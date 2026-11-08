#include "../src/sst_model_bindings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char bytes[4096];static unsigned calls;
static bool read_bytes(void *ctx,uint64_t off,void *out,size_t n){(void)ctx;calls++;if(off>sizeof bytes || n>sizeof bytes-off)return false;memcpy(out,bytes+off,n);return true;}
static void p16(unsigned at,uint16_t x){bytes[at]=x;bytes[at+1]=x>>8;}
static void p32(unsigned at,uint32_t x){for(unsigned i=0;i<4;i++)bytes[at+i]=x>>(8*i);}
static void node(unsigned d,unsigned i,unsigned id,unsigned left,unsigned right,unsigned name,unsigned data){unsigned b=d+8+16*i;p16(b,id);p16(b+4,left);p16(b+6,right);p32(b+8,name?name-d:0);p32(b+12,data?data-d:0);}
static void fixture(void) {
 memset(bytes,0,sizeof bytes);memcpy(bytes,"bres",4);p16(4,0xfeff);p32(8,sizeof bytes);memcpy(bytes+0x10,"root",4);
 p32(0x18,40);p32(0x1c,1);node(0x18,0,0xffff,1,0,0,0);node(0x18,1,0,1,1,0x204,0x80);p32(0x200,0x0e000000);strcpy((char*)bytes+0x204,"3DModels(NW4R)");
 p32(0x80,88);p32(0x84,4);node(0x80,0,0xffff,1,0,0,0);
 const char *names[4]={"pl_body","pl_head","al_face","pl_hands"};
 const unsigned id[4]={34,27,0,33},left[4]={3,1,4,4},right[4]={2,2,3,4};
 for(unsigned i=0;i<4;i++){unsigned name=0x300+i*0x30,mdl=0x800+i*0x100;strcpy((char*)bytes+name,names[i]);node(0x80,i+1,id[i],left[i],right[i],name,mdl);memcpy(bytes+mdl,"MDL0",4);p32(mdl+4,0x80);p32(mdl+8,11);p32(mdl+12,0-mdl);p32(mdl+0x48,name-mdl);}
}
int main(void) {
 uint64_t expected[4]={0x800,0x900,0xa00,0xb00};SstModelBindings out;
 fixture();assert(sst_model_bindings_validate(read_bytes,NULL,sizeof bytes,expected,&out));assert(out.matched_mask==15 && out.corroborated_mask==15 && out.bytes_read<SST_MODEL_READ_BUDGET);
 for(unsigned bad=0;bad<13;bad++){
  fixture();uint64_t wanted[4];memcpy(wanted,expected,sizeof wanted);
  switch(bad){case 0:bytes[0]='X';break;case 1:p16(4,0xfffe);break;case 2:p32(8,UINT32_MAX);break;case 3:p32(0x84,257);break;case 4:p32(0x80,8);break;case 5:p16(0x80+8+16+4,99);break;case 6:p16(0x80+8+16+6,1);break;case 7:strcpy((char*)bytes+0x330,"pl_body");break;case 8:p32(0x80+8+16+8,0x80000000);break;case 9:wanted[2]=0xb00;break;case 10:p32(0xa00+8,9);break;case 11:p32(0xb00+4,UINT32_MAX);break;case 12:memset(bytes+0x300,'Q',64);break;}
  assert(!sst_model_bindings_validate(read_bytes,NULL,sizeof bytes,wanted,&out));assert(out.bytes_read<=SST_MODEL_READ_BUDGET);
 }
 fixture();p32(0xa00+12,0);assert(sst_model_bindings_validate(read_bytes,NULL,sizeof bytes,expected,&out));assert(out.matched_mask==15 && out.corroborated_mask==11); // dictionary equality primary, backfile only corroborates
 calls=0;assert(!sst_model_bindings_validate(NULL,NULL,sizeof bytes,expected,&out));assert(!calls);
 puts("named-model binding PASS: converted header, exact native trie/directory identity, four roles, bounds/corruption/duplicate/wrong-model rejection; backfile only corroborates");return 0;
}
