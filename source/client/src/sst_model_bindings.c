#include "sst_model_bindings.h"
#include <string.h>
#define NODES 256u
#define NAME 64u
typedef struct {SstModelRead read;void *context;uint64_t extent;uint32_t used;} Reader;
typedef struct {uint16_t id,left,right;int32_t name,data;char text[NAME];} Node;
typedef struct {uint64_t offset;uint32_t count;Node nodes[NODES+1];} Dict;
static uint16_t u16(const unsigned char *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool get(Reader *r,uint64_t at,void *out,size_t n) {
 if(at>r->extent || n>r->extent-at || n>SST_MODEL_READ_BUDGET-r->used)return false;
 if(!r->read(r->context,at,out,n))return false;r->used+=(uint32_t)n;return true;
}
static bool relative(Reader *r,uint64_t base,int32_t delta,uint64_t *out) {
 int64_t d=delta;
 if(d<0){uint64_t n=(uint64_t)(-d);if(n>base)return false;*out=base-n;}
 else {if(base>r->extent || (uint64_t)d>r->extent-base)return false;*out=base+(uint64_t)d;}
 return *out<r->extent;
}
static bool text(Reader *r,uint64_t at,char out[NAME]) {
 for(unsigned i=0;i<NAME;i++){if(!get(r,at+i,out+i,1))return false;if(!out[i])return i!=0;}
 return false;
}
static bool lookup(const Dict *d,const char *key,uint32_t *index) {
 size_t length=strlen(key);uint32_t parent=0,child=d->nodes[0].left;
 for(unsigned hops=0;hops<=d->count+1;hops++) {
  if(!child || child>d->count)return false;
  const Node *p=&d->nodes[parent],*n=&d->nodes[child];
  if(p->id<=n->id){if(strcmp(n->text,key))return false;*index=child;return true;}
  parent=child;unsigned byte=n->id>>3,bit=n->id&7;
  child=byte<length && (((unsigned char)key[byte]>>bit)&1)?n->right:n->left;
 }
 return false;
}
static bool dictionary(Reader *r,uint64_t at,Dict *d) {
 unsigned char header[8];if(!get(r,at,header,8))return false;
 uint32_t size=u32(header),count=u32(header+4);
 if(!count || count>NODES || size<8+(count+1)*16 || at>r->extent || size>r->extent-at)return false;
 memset(d,0,sizeof *d);d->offset=at;d->count=count;
 for(uint32_t i=0;i<=count;i++) {
  unsigned char b[16];if(!get(r,at+8+(uint64_t)i*16,b,16))return false;Node *n=&d->nodes[i];
  n->id=u16(b);n->left=u16(b+4);n->right=u16(b+6);n->name=(int32_t)u32(b+8);n->data=(int32_t)u32(b+12);
  if(n->left>count || n->right>count)return false;
  if(!i){if(n->id!=UINT16_MAX || n->name || n->data || !n->left)return false;continue;}
  uint64_t name,data;if(n->id>=NAME*8 || !relative(r,at,n->name,&name) || !relative(r,at,n->data,&data) || !text(r,name,n->text))return false;
  for(uint32_t j=1;j<i;j++)if(!strcmp(n->text,d->nodes[j].text))return false;
 }
 // Every stored key must resolve by the actual native trie rule to its OWN
 // entry. A linear directory match cannot excuse corrupt links/ambiguity.
 for(uint32_t i=1;i<=count;i++){uint32_t found;if(!lookup(d,d->nodes[i].text,&found) || found!=i)return false;}
 return true;
}
bool sst_model_bindings_validate(SstModelRead read,void *context,uint64_t available,const uint64_t resources[4],SstModelBindings *out) {
 if(out)memset(out,0,sizeof *out);if(!read || !resources || !out || available<0x20)return false;
 Reader r={read,context,available>SST_MODEL_FILE_LIMIT?SST_MODEL_FILE_LIMIT:available,0};unsigned char head[0x18];
 if(!get(&r,0,head,sizeof head) || memcmp(head,"bres",4) || u16(head+4)!=0xfeff || memcmp(head+0x10,"root",4))return false;
 uint32_t size=u32(head+8);if(size<0x40 || size>r.extent)return false;r.extent=size;out->file_size=size;
 Dict root,models;uint32_t group;if(!dictionary(&r,0x18,&root) || !lookup(&root,"3DModels(NW4R)",&group))goto fail;
 uint64_t group_name,model_dict;
 if(!relative(&r,root.offset,root.nodes[group].name,&group_name) || group_name<4 || !relative(&r,root.offset,root.nodes[group].data,&model_dict))goto fail;
 unsigned char length[4];if(!get(&r,group_name-4,length,4))goto fail;
 uint32_t value=u32(length);if(value!=14 && value!=UINT32_C(0x0e000000))goto fail;
 if(!dictionary(&r,model_dict,&models))goto fail;
 const char *names[4]={"pl_body","pl_head","al_face","pl_hands"};
 for(unsigned i=0;i<4;i++) {
  uint32_t node;uint64_t model;if(!lookup(&models,names[i],&node) || !relative(&r,models.offset,models.nodes[node].data,&model) || model!=resources[i])goto fail;
  unsigned char b[0x4c];if(!get(&r,model,b,sizeof b) || memcmp(b,"MDL0",4) || u32(b+8)!=11 || u32(b+4)<sizeof b || u32(b+4)>r.extent-model)goto fail;
  out->matched_mask|=1u<<i;
  uint64_t back,name;char actual[NAME];
  if(relative(&r,model,(int32_t)u32(b+12),&back) && back==0 && relative(&r,model,(int32_t)u32(b+0x48),&name) && text(&r,name,actual) && !strcmp(actual,names[i]))out->corroborated_mask|=1u<<i;
 }
 out->bytes_read=r.used;return out->matched_mask==15;
fail:out->bytes_read=r.used;return false;
}
