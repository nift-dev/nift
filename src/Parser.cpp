#include "RuntimeJson.h"
#include "Parser.h"
#include "ParserAsyncPool.h"
#include "ParserHelpers.h"
#include "FrontMatter.h"
#include "FileSystem.h"
#include "Json.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

using nift::detail::built_in_metadata_name;
using nift::detail::nift_binding_type;

Parser::Parser(RenderHost& host, TrackedInfo& tracked_info)
    : host_(host), tracked_info_(tracked_info) {
    variable_scopes_.emplace_back();
    if (!tracked_info_.name.empty()) {
        nift::RuntimeValue metadata=nift::RuntimeValue::make_object(); bool from_project=false; std::string page_metadata_error;
        // Per-page model-equivalent metadata (front matter + type + schema
        // validation) computed without constructing the project-wide query
        // model, so an ordinary build never pays for project features it does
        // not use. Hosts without the content model fall back to direct parsing.
        if (host_.page_project_metadata(tracked_info_, metadata, page_metadata_error)) from_project = true;
        if(!from_project){json::Document metadata_document=json::Document::make_object();std::string e;auto cp=host_.content_path(tracked_info_);if(filesystem::file_exists(cp)){auto parsed=frontmatter::parse_inline(filesystem::read_file(cp));if(tracked_info_.frontmatter&&parsed.present)metadata_document["_error"]=json::Document("multiple front matter sources");else if(tracked_info_.frontmatter){frontmatter::load_external(host_.root(),*tracked_info_.frontmatter,metadata_document,e);if(!e.empty())metadata_document["_error"]=json::Document(e);}else if(parsed.present&&!parsed.error.empty())metadata_document["_error"]=json::Document(parsed.error);else if(parsed.present)metadata_document=parsed.value;}metadata=nift::runtime_from_json(metadata_document);}
        json_bindings_["frontmatter"]=std::make_shared<const nift::RuntimeValue>(std::move(metadata));
        // Current page for hierarchy composition (page.parent etc.). The
        // reference is constructed directly without touching the hierarchy
        // index, which is built lazily only when a hierarchy member is
        // actually accessed (pay-for-use).
        if (!tracked_info_.name.empty()) {
            auto page_sp=std::make_shared<nift::RuntimeValue>(nift::RuntimeValue(std::string("\x1fnift:page:")+tracked_info_.name));
            variable_scopes_.back().emplace("page", VariableBinding{page_sp, nift_binding_type(*page_sp), false, false});
        }
    }
}

Parser::~Parser() {
    for(auto& st:owned_async_instances_){std::unique_lock<std::mutex> lk(st->mutex);while(!st->done){lk.unlock();if(nift::detail::NiftAsyncPool::is_worker_thread()&&nift::detail::NiftAsyncPool::instance().run_one()){lk.lock();continue;}lk.lock();st->cv.wait_for(lk,std::chrono::milliseconds(1),[&]{return st->done;});}}
    for(auto& st:owned_thread_instances_){std::lock_guard<std::mutex> guard(st->join_mutex);if(st->worker.joinable())st->worker.join();}
}


bool Parser::finalize_script_resources(std::string& error) {
    std::vector<std::string> open_paths;
    for (auto& kv : file_instances_) {
        auto& f = *kv.second;
        if (!f.open) continue;
        open_paths.push_back(f.path.generic_string() + (f.dirty ? " (unsaved changes discarded)" : ""));
        f.open = false; f.dirty = false; f.mode.clear(); f.working.clear(); f.saved.clear(); f.cursor = 0;
    }
    if (open_paths.empty()) return true;
    error = "managed file left open";
    if (open_paths.size() > 1) error += " (" + std::to_string(open_paths.size()) + " files)";
    error += ": " + open_paths.front();
    return false;
}

void Parser::fail(const fs::path& source_path, const std::string& source, std::size_t offset, const std::string& message) {
    result_.ok = false;
    result_.error.tracked_name = tracked_info_.name;
    result_.error.source_file = source_path;
    result_.error.line = 1;

    std::size_t line_start = 0;
    for (std::size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++result_.error.line;
            line_start = i + 1;
        }
    }

    result_.error.column = offset >= line_start ? offset - line_start + 1 : 1;
    result_.error.source_length = 1;
    if (offset < source.size() && source[offset] == '@') {
        std::size_t name_end = offset + 1;
        while (name_end < source.size() && source[name_end] >= 'a' && source[name_end] <= 'z') ++name_end;
        std::size_t span_end = name_end;
        if (name_end < source.size() && source[name_end] == '(') {
            std::size_t close = 0;
            if (find_balanced(source, name_end, '(', ')', close)) span_end = close + 1;
        }
        result_.error.source_length = std::max<std::size_t>(1, span_end - offset);
    } else if (offset + 1 < source.size() && source[offset] == '$' && source[offset + 1] == '[') {
        const auto close = source.find(']', offset + 2);
        if (close != std::string::npos) result_.error.source_length = close + 1 - offset;
    }
    const std::size_t line_end = source.find('\n', line_start);
    result_.error.source_line = source.substr(
        line_start,
        line_end == std::string::npos ? std::string::npos : line_end - line_start);
    if (!result_.error.source_line.empty() && result_.error.source_line.back() == '\r')
        result_.error.source_line.pop_back();

    result_.error.message = message;
}

std::string Parser::metadata(const std::string& key) const {
    if (key == "title") return tracked_info_.title;
    if (key == "name") return tracked_info_.name;
    if (key == "content-path") return host_.relative(host_.content_path(tracked_info_));
    if (key == "output-path") return host_.relative(host_.output_path(tracked_info_));
    if (key == "template-path") return tracked_info_.template_path;

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    std::tm utc{};
#ifdef _WIN32
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    char buffer[64] = {};
    auto format = [&](const char* pattern, const std::tm& tm) {
        std::strftime(buffer, sizeof buffer, pattern, &tm);
        return std::string(buffer);
    };
    if (key == "build-time") return format("%H:%M:%S", local);
    if (key == "build-date") return format("%Y-%m-%d", local);
    if (key == "build-UTC-time") return format("%H:%M:%S", utc);
    if (key == "build-UTC-date") return format("%Y-%m-%d", utc);
    if (key == "build-YYYY") return format("%Y", local);
    if (key == "build-YY") return format("%y", local);
    if (key == "build-timezone") return format("%Z", local);
#if defined(_WIN32)
    if (key == "build-OS") return "Windows";
#elif defined(__APPLE__)
    if (key == "build-OS") return "macOS";
#else
    if (key == "build-OS") return "Linux";
#endif
    return {};
}



void Parser::push_variable_scope() { variable_scopes_.emplace_back(); }
void Parser::pop_variable_scope() { if (variable_scopes_.size() > 1) variable_scopes_.pop_back(); }

Parser::LexicalEnvironmentState Parser::enter_lexical_environment(std::shared_ptr<ModuleEnv> module_env) {
    LexicalEnvironmentState state{active_module_env_, false};
    if (!module_env && !active_module_env_) return state;
    if (!module_env) {
        auto caller=std::find_if(saved_lexical_scopes_.rbegin(),saved_lexical_scopes_.rend(),[](const auto& frame){return !frame.module_env;});
        if(caller==saved_lexical_scopes_.rend())return state;
        auto caller_scopes=caller->scopes;
        saved_lexical_scopes_.push_back(SavedLexicalScopes{std::move(variable_scopes_),active_module_env_});
        variable_scopes_=std::move(caller_scopes);
        active_module_env_.reset();
        state.active=true;
        return state;
    }
    saved_lexical_scopes_.push_back(SavedLexicalScopes{std::move(variable_scopes_),active_module_env_});
    if (saved_lexical_scopes_.back().scopes.empty()) variable_scopes_.emplace_back();
    else variable_scopes_.push_back(saved_lexical_scopes_.back().scopes.front());
    active_module_env_ = std::move(module_env);
    variable_scopes_.push_back(active_module_env_->vars);
    state.active=true;
    return state;
}

void Parser::leave_lexical_environment(LexicalEnvironmentState state) {
    if(!state.active)return;
    variable_scopes_ = std::move(saved_lexical_scopes_.back().scopes);
    saved_lexical_scopes_.pop_back();
    active_module_env_ = std::move(state.module_env);
}

void Parser::push_json_scope() {
    json_binding_scopes_.emplace_back();
    push_variable_scope();
}

void Parser::pop_json_scope() {
    if (json_binding_scopes_.empty()) return;
    for (const auto& name : json_binding_scopes_.back()) json_bindings_.erase(name);
    json_binding_scopes_.pop_back();
    pop_variable_scope();
}

std::string Parser::trim_copy(const std::string& text) const {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool Parser::find_balanced(const std::string& source,
                           std::size_t open_position,
                           char open_char,
                           char close_char,
                           std::size_t& close_position) const {
    if (open_position >= source.size() || source[open_position] != open_char) return false;

    std::size_t depth = 0;
    bool quoted = false;
    char quote = 0;

    for (std::size_t i = open_position; i < source.size(); ++i) {
        const char c = source[i];
        if (quoted) {
            if (c == '\\' && i + 1 < source.size()) {
                ++i;
            } else if (c == quote) {
                quoted = false;
            }
            continue;
        }

        if (c == '\'' || c == '"') {
            quoted = true;
            quote = c;
            continue;
        }

        // Code spans and fenced blocks (backtick-delimited) are not template
        // structure: braces inside Markdown code samples or AsciiDoc/RST
        // literal backtick spans must not terminate a surrounding @markup or
        // @json block. An unterminated run is treated as literal text.
        if (c == '`') {
            std::size_t run = 1;
            while (i + run < source.size() && source[i + run] == '`') ++run;
            const std::string delimiter(source, i, run);
            const auto span_end = source.find(delimiter, i + run);
            if (span_end != std::string::npos) {
                i = span_end + run - 1;
            }
            continue;
        }

        // Comment bodies are not template structure. In particular, a brace in
        // a source/HTML comment must not terminate a surrounding @if/@for block.
        if (source.compare(i, 3, "@/*") == 0) {
            const auto comment_end = source.find("*/", i + 3);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 1;
            continue;
        }
        if (source.compare(i, 4, "<!--") == 0) {
            const auto comment_end = source.find("-->", i + 4);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 2;
            continue;
        }
        if (source.compare(i, 3, "@//") == 0) {
            const auto line_end = source.find('\n', i);
            if (line_end == std::string::npos) return false;
            i = line_end;
            continue;
        }

        if (c == open_char) {
            ++depth;
        } else if (c == close_char) {
            if (--depth == 0) {
                close_position = i;
                return true;
            }
        }
    }
    return false;
}

bool Parser::resolve_json_value(const std::string& expression,
                                std::shared_ptr<const nift::RuntimeValue>& value,
                                std::string& error) {
    std::size_t position = 0;

    auto identifier = [&](std::string& result) -> bool {
        if (position >= expression.size()) return false;
        const unsigned char first = static_cast<unsigned char>(expression[position]);
        if (!(std::isalpha(first) || expression[position] == '_')) return false;
        const std::size_t start = position++;
        while (position < expression.size()) {
            const unsigned char c = static_cast<unsigned char>(expression[position]);
            if (!(std::isalnum(c) || expression[position] == '_')) break;
            ++position;
        }
        result = expression.substr(start, position - start);
        return true;
    };

    std::string root_name;
    if (!identifier(root_name)) return false;

    std::shared_ptr<const nift::RuntimeValue> current;
    bool contract_binding = false;
    // Host-supplied bindings (Embedded Nift engine defaults / Context overlays)
    // resolve before @json bindings, contracts and built-in metadata.
    VariableBinding* local_binding=nullptr;
    for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(root_name);if(it!=scope->end()){local_binding=&it->second;break;}}
    if (local_binding) {
        // Location references must be re-synced before use: the aliasing
        // shared_ptr stored at loop setup can dangle if the parent aggregate
        // was mutated by the body since setup. sync() re-resolves through the
        // root slot + path so template paths never read a stale interior alias.
        local_binding->sync();
    }
    if (local_binding && local_binding->value && (local_binding->value->is_object() || local_binding->value->is_array())) {
        current = local_binding->value;
    } else if (const auto* supplied = host_.binding(root_name)) {
        current = *supplied;
        if (root_name == "project") {
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/config.json"));
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/tracked.json"));
            // Compact semantic project dependency: the whole project model is
            // captured by a single fingerprint file that is rewritten only when
            // the model changes, so a project-using page records O(1) deps
            // instead of one entry per tracked file.
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/project.fingerprint"));
        }
    } else {
        const auto binding = json_bindings_.find(root_name);
        if (binding != json_bindings_.end()) {
            current = binding->second;
        } else {
            const std::string* contract_source = host_.contract_source(root_name);
            if (!contract_source) return false;
            contract_binding = true;

            const std::string& contract_path_argument = *contract_source;
            const fs::path contract_path = (host_.root() / contract_path_argument).lexically_normal();
            if (!filesystem::path_within(host_.root(), contract_path)) {
                error = "contract '" + root_name + "': path must stay inside the Nift project: " +
                        contract_path_argument;
                return true;
            }
            if (!host_.source_exists(contract_path)) {
                error = "contract '" + root_name + "': file does not exist: " + contract_path_argument;
                return true;
            }

            const auto cached = contract_bindings_.find(root_name);
            if (cached != contract_bindings_.end()) {
                current = cached->second;
            } else {
                std::string contract_error;
                auto document = host_.read_shared_runtime_json(contract_path, contract_error);
                if (!document) {
                    error = "contract '" + root_name + "': failed to parse " + contract_path_argument +
                            (contract_error.empty() ? "" : " (" + contract_error + ")");
                    return true;
                }
                contract_bindings_.emplace(root_name, document);
                current = std::move(document);
            }

            result_.dependencies.insert(host_.relative(host_.root() / ".nift/config.json"));
            result_.dependencies.insert(host_.relative(contract_path));
        }
    }

    while (position < expression.size()) {
        if (expression[position] == '.') {
            ++position;
            std::string member;
            if (!identifier(member)) {
                error = "invalid JSON member access in '" + expression + "'";
                return true;
            }
            if (current->is_string() && current->string.rfind("\x1fnift:page:",0)==0) {
                result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                nift::RuntimeValue resolved; std::string perr;
                if (!host_.resolve_page_member(current->string.substr(11), member, resolved, perr)) {
                    error = perr.empty() ? ("page has no member: " + member) : perr;
                    return true;
                }
                current = std::make_shared<const nift::RuntimeValue>(std::move(resolved));
                continue;
            }
            if (!current->is_object()) {
                error = "cannot access member '" + member + "' because the current JSON value is not an object";
                return true;
            }
            if (!current->has(member)) {
                if (contract_binding) {
                    const std::string key = expression.size() > root_name.size() + 1
                        ? expression.substr(root_name.size() + 1) : std::string{};
                    error = "contract '" + root_name + "' has no entry '" + key + "'";
                } else {
                    error = "JSON value '" + expression.substr(0, position - member.size() - 1) +
                            "' has no member '" + member + "'";
                }
                return true;
            }

            const nift::RuntimeValue* child = &(*current)[member];
            current = std::shared_ptr<const nift::RuntimeValue>(current, child);
            continue;
        }

        if (expression[position] == '[') {
            ++position; const std::size_t token_start=position; bool quoted=false; char quote=0;
            if(position<expression.size()&&(expression[position]=='"'||expression[position]=='\'')){quoted=true;quote=expression[position++];while(position<expression.size()&&expression[position]!=quote){if(expression[position]=='\\'&&position+1<expression.size())position+=2;else ++position;}if(position>=expression.size()){error="unterminated JSON object key in '"+expression+"'";return true;}++position;}else while(position<expression.size()&&expression[position]!=']')++position;
            if(position>=expression.size()||expression[position]!=']'){error="JSON index has no closing ']' in '"+expression+"'";return true;}std::string token=trim_copy(expression.substr(token_start,position-token_start));++position;
            if(current->is_array()){
                std::size_t index=0;
                if(quoted||token.empty()||!std::all_of(token.begin(),token.end(),[](unsigned char c){return std::isdigit(c); })){
                    if(quoted||token.empty()){
                        error="JSON array indices must be non-negative integers in '"+expression+"'";return true;
                    }
                    nift::RuntimeValue computed;
                    if(!evaluate_expression(token,computed,error)||!nift::runtime_number_to_size(computed,index)){
                        if(error.empty())error="JSON array indices must be non-negative integers in '"+expression+"'";
                        return true;
                    }
                }else{try{index=(std::size_t)std::stoull(token);}catch(...){error="JSON array index is out of range in '"+expression+"'";return true;}}
                if(index>=current->array.size()){error="JSON array index "+std::to_string(index)+" is out of range in '"+expression+"'";return true;}const nift::RuntimeValue* child=&(*current)[index];current=std::shared_ptr<const nift::RuntimeValue>(current,child);continue;
            }
            if(current->is_bytes()){
                std::size_t index=0;
                if(quoted||token.empty()||!std::all_of(token.begin(),token.end(),[](unsigned char c){return std::isdigit(c);})){
                    nift::RuntimeValue computed;if(quoted||!evaluate_expression(token,computed,error)||!nift::runtime_number_to_size(computed,index)){if(error.empty())error="bytes indices must be non-negative integers in '"+expression+"'";return true;}
                }else{try{index=static_cast<std::size_t>(std::stoull(token));}catch(...){error="bytes index is out of range in '"+expression+"'";return true;}}
                if(!current->bytes||index>=current->bytes->size()){error="bytes index "+std::to_string(index)+" is out of range in '"+expression+"'";return true;}
                current=std::make_shared<const nift::RuntimeValue>(static_cast<int>((*current->bytes)[index]));continue;
            }
            if(current->is_object()){
                std::string key;if(quoted){if(token.size()<2){error="invalid JSON object key";return true;}key=token.substr(1,token.size()-2);}else{VariableBinding* kb=nullptr;for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(token);if(it!=scope->end()){kb=&it->second;break;}}if(!kb||!kb->value||!kb->value->is_string()){
                    nift::RuntimeValue computed;
                    if(!evaluate_expression(token,computed,error)||!computed.is_string()){if(error.empty())error="JSON object index must be a quoted string, string binding, or string expression in '"+expression+"'";return true;}
                    key=computed.string;
                }else{key=kb->value->string;}}if(!current->has(key)){error="JSON object has no key '"+key+"'";return true;}const nift::RuntimeValue* child=&(*current)[key];current=std::shared_ptr<const nift::RuntimeValue>(current,child);continue;
            }
            error="cannot index JSON value in '"+expression+"'";return true;
        }

        error = "invalid JSON access syntax in '" + expression + "'";
        return true;
    }

    value = std::move(current);
    return true;
}

bool Parser::json_value(const std::string& expression, std::string& value, std::string& error) {
    std::shared_ptr<const nift::RuntimeValue> document;
    if (!resolve_json_value(expression, document, error)) return false;
    if (!error.empty()) return true;

    if (document->is_string()) {
        value = document->string;
    } else if (document->is_number() || document->is_bool() || document->is_null()) {
        value = document->dump(0);
    } else if (document->is_array()) {
        error = "cannot render JSON array $[" + expression + "]; select an element first";
    } else if (document->is_object()) {
        error = "cannot render JSON object $[" + expression + "]; select a member first";
    } else if (document->is_bytes()) {
        error = "cannot render bytes $[" + expression + "]; decode as UTF-8 first";
    }
    return true;
}

bool Parser::interpolate_parameter(const std::string& parameter,
                                   std::string& resolved,
                                   std::string& error) {
    resolved.clear();
    resolved.reserve(parameter.size());

    for (std::size_t i = 0; i < parameter.size();) {
        if (parameter[i] == '\\' && i + 1 < parameter.size() && parameter[i + 1] == '$') {
            resolved.push_back('$');
            i += 2;
            continue;
        }
        if (parameter.compare(i, 2, "$[") != 0) {
            const auto next = parameter.find_first_of("\\$", i + 1);
            const std::size_t end = next == std::string::npos ? parameter.size() : next;
            resolved.append(parameter, i, end - i);
            i = end;
            continue;
        }

        std::size_t end = i + 2;
        std::size_t nested_brackets = 0;
        for (; end < parameter.size(); ++end) {
            if (parameter[end] == '[') {
                ++nested_brackets;
            } else if (parameter[end] == ']') {
                if (nested_brackets == 0) break;
                --nested_brackets;
            }
        }
        if (end == parameter.size()) {
            error = "unterminated parameter value expression";
            return false;
        }

        const std::string expression = parameter.substr(i + 2, end - i - 2);
        nift::RuntimeValue expression_value;
        std::string expression_error;
        if (evaluate_expression(expression, expression_value, expression_error)) {
            if (expression_value.is_array() || expression_value.is_object() || expression_value.is_bytes()) {
                error = "parameter expression must resolve to a scalar value";
                return false;
            }
            resolved += render_expression_value(expression_value);
        } else if (!expression_error.empty() && expression_error.rfind("unknown value or malformed expression:", 0) != 0) {
            error = expression_error;
            return false;
        } else if (built_in_metadata_name(expression)) {
            resolved += metadata(expression);
        } else {
            std::string value;
            std::string value_error;
            if (!json_value(expression, value, value_error)) {
                error = "unknown parameter value $[" + expression + "]";
                return false;
            }
            if (!value_error.empty()) {
                error = std::move(value_error);
                return false;
            }
            resolved += value;
        }
        i = end + 1;
    }
    return true;
}
