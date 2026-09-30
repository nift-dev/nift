#include <nift/c_abi.h>
#include <assert.h>
#include <string.h>
int main(void){
  nift_engine* e=nift_engine_new(); assert(e);
  const char* s="fn(add(a,b)) { return a + b }\nprint(\"out\")\nerr(\"err\")\nreturn 7\n";
  nift_script_result* r=0; assert(nift_engine_execute(e,s,strlen(s),"embed",5,0,0,0,&r)==NIFT_OK); assert(r&&nift_script_result_ok(r));
  nift_string out={0}; nift_bytes stream={0}; assert(nift_script_result_value_json(r,&out)==NIFT_OK); assert(out.length==1&&out.data[0]=='7');
  assert(nift_script_result_stdout(r,&stream)==NIFT_OK); assert(stream.length==4&&!memcmp(stream.data,"out\n",4));
  assert(nift_script_result_stderr(r,&stream)==NIFT_OK); assert(stream.length==4&&!memcmp(stream.data,"err\n",4)); nift_script_result_free(r);
  const char* ex="add(2,3)"; r=0; assert(nift_engine_evaluate(e,ex,strlen(ex),&r)==NIFT_OK); assert(nift_script_result_ok(r)); assert(nift_script_result_value_json(r,&out)==NIFT_OK); assert(out.length==1&&out.data[0]=='5'); assert(nift_script_result_stdout(r,&stream)==NIFT_OK&&stream.length==0); assert(nift_script_result_stderr(r,&stream)==NIFT_OK&&stream.length==0); nift_script_result_free(r);
  nift_engine_free(e); return 0;
}
