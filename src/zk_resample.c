/* SPDX-License-Identifier: MIT */
#include "zk_resample.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RADIUS 3.0
#define ENCODE_STEPS 4096

struct Axis {
    int *first, *count;
    size_t *offset;
    float *weight;
};
static float decode[256], encode[ENCODE_STEPS+1];
static int tables_ready;

static void make_tables(void)
{
    if(tables_ready)return;
    for(int i=0;i<256;i++){
        double s=i/255.0;
        decode[i]=(float)(s<=0.04045 ? s/12.92 : pow((s+0.055)/1.055,2.4));
    }
    for(int i=0;i<=ENCODE_STEPS;i++){
        double l=(double)i/ENCODE_STEPS;
        encode[i]=(float)(255*(l<=0.0031308 ? 12.92*l : 1.055*pow(l,1.0/2.4)-0.055));
    }
    tables_ready=1;
}
static double lanczos(double x)
{
    x=fabs(x);
    if(x<1e-12)return 1;
    if(x>=RADIUS)return 0;
    const double pi=3.14159265358979323846;
    double p=pi*x;
    return sin(p)*sin(p/RADIUS)/(p*p/RADIUS);
}
static void free_axis(struct Axis *a)
{
    free(a->first);free(a->count);free(a->offset);free(a->weight);
    memset(a,0,sizeof *a);
}
static int make_axis(struct Axis *a,int source,int target)
{
    double scale=(double)target/source;
    double filter_scale=scale<1?scale:1;
    double support=RADIUS/filter_scale;
    size_t total=0;
    a->first=malloc((size_t)target*sizeof *a->first);
    a->count=malloc((size_t)target*sizeof *a->count);
    a->offset=malloc((size_t)target*sizeof *a->offset);
    if(!a->first || !a->count || !a->offset)return 0;
    for(int x=0;x<target;x++){
        double center=(x+0.5)/scale-0.5;
        a->first[x]=(int)ceil(center-support);
        a->count[x]=(int)floor(center+support)-a->first[x]+1;
        a->offset[x]=total;
        total+=(size_t)a->count[x];
    }
    a->weight=malloc(total*sizeof *a->weight);
    if(!a->weight)return 0;
    for(int x=0;x<target;x++){
        double center=(x+0.5)/scale-0.5,sum=0;
        for(int k=0;k<a->count[x];k++){
            double value=lanczos((a->first[x]+k-center)*filter_scale);
            a->weight[a->offset[x]+(size_t)k]=(float)value;sum+=value;
        }
        if(fabs(sum)<1e-12)return 0;
        for(int k=0;k<a->count[x];k++)a->weight[a->offset[x]+(size_t)k]/=(float)sum;
    }
    return 1;
}
static int edge(int position,int size)
{
    return position<0?0:position>=size?size-1:position;
}
static float unit(float value)
{
    return value<0?0:value>1?1:value;
}
static unsigned char to_srgb(float value)
{
    float position=unit(value)*ENCODE_STEPS;
    int index=(int)position;
    if(index>=ENCODE_STEPS)return 255;
    return (unsigned char)(encode[index]+(encode[index+1]-encode[index])*(position-index)+0.5f);
}
int zk_resample_argb(const unsigned char *source,int sw,int sh,int w,int h,
                      unsigned char **output)
{
    struct Axis horizontal={0},vertical={0};
    float *rows=NULL;
    unsigned char *pixels=NULL;
    int ok=0;
    if(!output)return 0;
    *output=NULL;
    if(!source || sw<1 || sh<1 || sw>1024 || sh>1024 || w<1 || h<1 || w>512 || h>512)return 0;
    pixels=malloc((size_t)w*h*4);
    if(!pixels)return 0;
    if(sw==w && sh==h){memcpy(pixels,source,(size_t)w*h*4);*output=pixels;return 1;}
    make_tables();
    if(!make_axis(&horizontal,sw,w) || !make_axis(&vertical,sh,h))goto done;
    rows=malloc((size_t)w*sh*4*sizeof *rows);
    if(!rows)goto done;
    for(int y=0;y<sh;y++)for(int x=0;x<w;x++){
        float sum[4]={0};
        for(int k=0;k<horizontal.count[x];k++){
            int sx=edge(horizontal.first[x]+k,sw);
            const unsigned char *p=source+4*((size_t)y*sw+sx);
            float weight=horizontal.weight[horizontal.offset[x]+(size_t)k];
            float alpha=p[0]/255.0f;
            sum[0]+=alpha*weight;
            for(int c=1;c<4;c++)sum[c]+=decode[p[c]]*alpha*weight;
        }
        memcpy(rows+4*((size_t)y*w+x),sum,sizeof sum);
    }
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        float sum[4]={0};
        for(int k=0;k<vertical.count[y];k++){
            const float *p=rows+4*((size_t)edge(vertical.first[y]+k,sh)*w+x);
            float weight=vertical.weight[vertical.offset[y]+(size_t)k];
            for(int c=0;c<4;c++)sum[c]+=p[c]*weight;
        }
        unsigned char *p=pixels+4*((size_t)y*w+x);
        /* Unpremultiply with the original filtered alpha: clipping it first
         * would brighten constant colours where negative lobes overshoot.
         * Clamp output alpha and straight colour independently. */
        float alpha=unit(sum[0]);
        p[0]=(unsigned char)(alpha*255+0.5f);
        for(int c=1;c<4;c++)p[c]=p[0]?to_srgb(sum[c]/sum[0]):0;
    }
    *output=pixels;pixels=NULL;ok=1;
done:
    free(pixels);free(rows);free_axis(&horizontal);free_axis(&vertical);
    return ok;
}
