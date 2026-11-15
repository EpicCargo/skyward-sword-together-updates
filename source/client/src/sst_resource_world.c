// SPDX-License-Identifier: MIT
#include "sst_resource_world.h"
#include <math.h>
#include <string.h>
static bool extent(const void *p,size_t bytes){return p&&bytes<=UINTPTR_MAX-(uintptr_t)p;}
static bool overlap(const void *a,size_t an,const void *b,size_t bn){return an&&bn&&(uintptr_t)a<(uintptr_t)b+bn&&(uintptr_t)b<(uintptr_t)a+an;}
static bool finite_value(double x){return isfinite(x)&&fabs(x)<=1e6;}
static bool rotation(const float *m,unsigned stride){
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)if(!finite_value(m[stride*i+j]))return false;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<=i;j++){
        double dot=0;for(unsigned k=0;k<3;k++)dot+=(double)m[stride*i+k]*m[stride*j+k];
        if(fabs(dot-(i==j?1:0))>.005)return false;
    }
    double det=m[0]*((double)m[stride+1]*m[2*stride+2]-(double)m[stride+2]*m[2*stride+1])-
        m[1]*((double)m[stride]*m[2*stride+2]-(double)m[stride+2]*m[2*stride])+
        m[2]*((double)m[stride]*m[2*stride+1]-(double)m[stride+1]*m[2*stride]);
    return fabs(det-1)<.01;
}
bool sst_resource_world_ops(const float (*input)[12],unsigned count,const SstResourceWorldOp *ops,unsigned op_count,float (*out)[12],unsigned capacity){
    if(!count||count>SST_CHR0_MAX_JOINTS||capacity<count||capacity>SST_CHR0_MAX_JOINTS||op_count>SST_WORLD_MAX_OPS)return false;
    size_t ib=(size_t)count*sizeof *input,ob=(size_t)capacity*sizeof *out,pb=(size_t)op_count*sizeof *ops;
    if(!extent(input,ib)||!extent(out,ob)||(pb&&!extent(ops,pb))||overlap(input,ib,out,ob)||overlap(ops,pb,out,ob)||overlap(input,ib,ops,pb))return false;
    memset(out,0,ob);
    for(unsigned i=0;i<count;i++){
        if(!rotation(input[i],4))return false;
        for(unsigned k=0;k<3;k++)if(!finite_value(input[i][4*k+3]))return false;
    }
    for(unsigned i=0;i<op_count;i++){
        const SstResourceWorldOp *op=&ops[i];
        if(op->joint>=count||(op->anchor!=SST_WORLD_KEEP_POSITION&&op->anchor>=count)||!rotation(op->rotation,3))return false;
        for(unsigned k=0;k<3;k++)if(!finite_value(op->local_offset[k])||!finite_value(op->world_offset[k])||
            (op->anchor==SST_WORLD_KEEP_POSITION&&op->local_offset[k]!=0))return false;
    }
    float work[SST_CHR0_MAX_JOINTS][12];memcpy(work,input,ib);
    for(unsigned i=0;i<op_count;i++){
        const SstResourceWorldOp *op=&ops[i];float next[12];
        for(unsigned row=0;row<3;row++){
            for(unsigned col=0;col<3;col++){
                double x=0;for(unsigned k=0;k<3;k++)x+=(double)op->rotation[3*row+k]*work[op->joint][4*k+col];
                if(!finite_value(x))return false;next[4*row+col]=(float)x;
            }
            double x=op->world_offset[row];
            if(op->anchor==SST_WORLD_KEEP_POSITION)x+=work[op->joint][4*row+3];
            else {x+=work[op->anchor][4*row+3];for(unsigned k=0;k<3;k++)x+=(double)work[op->anchor][4*row+k]*op->local_offset[k];}
            if(!finite_value(x))return false;next[4*row+3]=(float)x;
        }
        // Several approximate rotations can accumulate scale/skew. Do not
        // publish a chain that the existing rigid palette builder would reject.
        if(!rotation(next,4))return false;
        memcpy(work[op->joint],next,sizeof next);
    }
    memcpy(out,work,ib);return true;
}
