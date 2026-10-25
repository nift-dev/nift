/* Pure C consumer: ABI calls, ownership, refusal, provenance and repeated VM runs. */
#include "nift/c_abi.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); } } while (0)
static nift_source source(const char* text) {
    nift_source s = {NIFT_SOURCE_TEXT,text,strlen(text),"owned.f",7}; return s;
}
static int equals(nift_status status, nift_string text, const char* expected) {
    return status == NIFT_OK && text.length == strlen(expected) &&
        (!text.length || !memcmp(text.data,expected,text.length));
}
static nift_program* compile(const char* text) {
    nift_program* p = NULL; nift_program_result* r = NULL; nift_source s = source(text);
    CHECK(nift_program_compile(&s,&p,&r) == NIFT_OK);
    CHECK(r && nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
    CHECK(p != NULL); nift_program_result_free(r); return p;
}
static void* wrong_thread(void* arg) {
    nift_program_result* result = (nift_program_result*)1;
    CHECK(nift_program_run((nift_program*)arg,0,0,0,&result) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(result == NULL);
    CHECK(nift_program_free((nift_program*)arg) == NIFT_ERROR_INVALID_ARGUMENT);
    return NULL;
}
int main(void) {
    nift_program* p = (nift_program*)1; nift_program_result* r = (nift_program_result*)1;
    nift_string v = {(const char*)1,1}; nift_source s = source("print(1);");
    CHECK(nift_program_compile(NULL,&p,&r) == NIFT_ERROR_INVALID_ARGUMENT && !p && !r);
    CHECK(nift_program_compile(&s,NULL,&r) == NIFT_ERROR_INVALID_ARGUMENT && !r);
    CHECK(nift_program_compile(&s,&p,NULL) == NIFT_ERROR_INVALID_ARGUMENT && !p);
    s.data = NULL;
    CHECK(nift_program_compile(&s,&p,&r) == NIFT_ERROR_INVALID_ARGUMENT && !p && !r);
    s = source("print(1);"); s.kind = NIFT_SOURCE_PATH;
    CHECK(nift_program_compile(&s,&p,&r) == NIFT_ERROR_INVALID_ARGUMENT && !p && !r);
    CHECK(nift_program_free(NULL) == NIFT_OK); nift_program_result_free(NULL);
    CHECK(nift_program_run(NULL,0,0,0,&r) == NIFT_ERROR_INVALID_ARGUMENT && !r);
    CHECK(nift_program_result_output(NULL,&v) == NIFT_ERROR_INVALID_ARGUMENT && !v.data && !v.length);
    CHECK(nift_program_result_outcome(NULL) == NIFT_PROGRAM_INTERNAL);

    /* Caller releases/changes both input buffers before running. */
    { char text[] = "x := 0; x += 1; print(x); return x + 41;"; char name[] = "owned.f";
      s = source(text); s.logical_name = name;
      CHECK(nift_program_compile(&s,&p,&r) == NIFT_OK && p);
      nift_program_result_free(r); memset(text,'?',sizeof(text)-1); memset(name,'?',7); }
    for (int i=0;i<200;++i) {
        CHECK(nift_program_run(p,0,0,0,&r) == NIFT_OK && r);
        CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
        nift_status st = nift_program_result_output(r,&v); CHECK(equals(st,v,"1\n"));
        st = nift_program_result_return_text(r,&v); CHECK(equals(st,v,"42"));
        CHECK(nift_program_result_returned(r)); nift_program_result_free(r);
    }
    { pthread_t t; int started = pthread_create(&t,NULL,wrong_thread,p);
      int joined = started == 0 ? pthread_join(t,NULL) : -1; CHECK(started == 0); CHECK(joined == 0); }
    CHECK(nift_program_run(p,0,1,0,&r) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(r == NULL);
    CHECK(nift_program_run(p,0,0,0,&r) == NIFT_OK);
    CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
    CHECK(nift_program_free(p) == NIFT_OK);
    { nift_status st = nift_program_result_output(r,&v); CHECK(equals(st,v,"1\n")); }
    nift_program_result_free(r);

    p = compile("print(7);\nfn(boom()) { throw error(\"boom\"); }\nboom();");
    CHECK(nift_program_run(p,0,0,0,&r) == NIFT_OK);
    CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_ERROR);
    CHECK(nift_program_result_catchable(r)); CHECK(nift_program_result_error_line(r) == 2);
    CHECK(nift_program_result_error_column(r) > 0);
    nift_program_free(p);
    { nift_status st = nift_program_result_output(r,&v); CHECK(equals(st,v,"7\n"));
      st = nift_program_result_error_message(r,&v); CHECK(equals(st,v,"boom"));
      st = nift_program_result_error_source(r,&v); CHECK(equals(st,v,"owned.f")); }
    nift_program_result_free(r);

    p = compile("try { throw error(\"caught\"); } catch(e) { print(e.message); }");
    CHECK(nift_program_run(p,0,0,0,&r) == NIFT_OK && nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
    { nift_status st = nift_program_result_output(r,&v); CHECK(equals(st,v,"caught\n")); }
    nift_program_result_free(r); nift_program_free(p);

    s = source("print(1); if (");
    CHECK(nift_program_compile(&s,&p,&r) == NIFT_OK && !p && r);
    CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_SYNTAX_ERROR);
    CHECK(nift_program_result_error_line(r) > 0); nift_program_result_free(r);
    s = source("print(999); import(\"/missing/cp18-should-not-open.f\");");
    CHECK(nift_program_compile(&s,&p,&r) == NIFT_OK && !p && r);
    CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_REFUSED);
    { nift_status st = nift_program_result_output(r,&v); CHECK(equals(st,v,"")); }
    nift_program_result_free(r);
    s = source(""); CHECK(nift_program_compile(&s,&p,&r) == NIFT_OK && p);
    nift_program_result_free(r); nift_program_free(p);
    printf("CP18 C: %d checks, %d failures\n",checks,failures); return failures ? 1 : 0;
}
