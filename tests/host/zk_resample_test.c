/* zk_resample: scaling keeps light, alpha and edges. */
#include "zk_resample.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reduction_keeps_energy(void)
{
    /* Nearest-neighbour loses alternating detail entirely. A symmetric 2x2
     * checker integrates to 50% linear light, or approximately 188 sRGB. */
    const unsigned char source[] = {255,0,0,0, 255,255,255,255,
                                    255,255,255,255, 255,0,0,0};
    unsigned char *out = NULL;
    assert(zk_resample_argb(source,2,2,1,1,&out));
    assert(out[0]==255 && out[1]>=187 && out[1]<=189);
    assert(out[1]==out[2] && out[2]==out[3]);
    free(out);
}
static void invisible_colour_does_not_bleed(void)
{
    const unsigned char source[] = {255,255,0,0, 0,0,0,255};
    unsigned char *out = NULL;
    assert(zk_resample_argb(source,2,1,1,1,&out));
    assert(out[0]>=127 && out[0]<=128);
    assert(out[1]==255 && out[2]==0 && out[3]==0);
    free(out);
}
static void alpha_overshoot_preserves_colour(void)
{
    unsigned char source[12*4], *out=NULL;
    for(int i=0;i<12;i++){
        source[4*i]=i<6?255:0;
        source[4*i+1]=source[4*i+2]=source[4*i+3]=128;
    }
    assert(zk_resample_argb(source,12,1,5,1,&out));
    for(int i=0;i<5;i++)if(out[4*i])
        for(int c=1;c<4;c++)assert(abs((int)out[4*i+c]-128)<=1);
    free(out);
}
static void identity_preserves_bytes(void)
{
    const unsigned char source[] = {0,45,67,89, 130,12,34,56, 255,255,240,3};
    unsigned char *out = NULL;
    assert(zk_resample_argb(source,3,1,3,1,&out));
    assert(out!=source && !memcmp(source,out,sizeof source));
    free(out);
}
static void constant_survives_edges_and_scaling(void)
{
    unsigned char source[7*3*4], saved[sizeof source];
    for(size_t i=0;i<sizeof source;i+=4){source[i]=143;source[i+1]=64;source[i+2]=151;source[i+3]=239;}
    memcpy(saved,source,sizeof source);
    const int sizes[][2]={{1,1},{4,2},{13,8},{512,1},{1,512}};
    for(size_t j=0;j<sizeof sizes/sizeof sizes[0];j++){
        unsigned char *out=NULL;
        assert(zk_resample_argb(source,7,3,sizes[j][0],sizes[j][1],&out));
        for(int i=0;i<sizes[j][0]*sizes[j][1];i++)
            for(int c=0;c<4;c++)assert(abs((int)out[4*i+c]-source[c])<=1);
        free(out);
    }
    assert(!memcmp(source,saved,sizeof source));
}
static void alpha_zero_is_clean_and_bounds_are_checked(void)
{
    unsigned char source[]={0,255,255,255, 0,255,0,255};
    unsigned char *out=NULL;
    assert(zk_resample_argb(source,2,1,3,2,&out));
    for(int i=0;i<3*2*4;i++)assert(out[i]==0);
    free(out);
    out=source;assert(!zk_resample_argb(source,0,1,1,1,&out) && !out);
    out=source;assert(!zk_resample_argb(source,1025,1,1,1,&out) && !out);
    out=source;assert(!zk_resample_argb(source,1,1,513,1,&out) && !out);
    out=source;assert(!zk_resample_argb(NULL,1,1,1,1,&out) && !out);
    assert(!zk_resample_argb(source,1,1,1,1,NULL));
}
int main(void)
{
    reduction_keeps_energy();
    invisible_colour_does_not_bleed();
    alpha_overshoot_preserves_colour();
    identity_preserves_bytes();
    constant_survives_edges_and_scaling();
    alpha_zero_is_clean_and_bounds_are_checked();
    puts("PASS resampling: reduction energy, alpha edges, identity, bounds and source ownership");
    return 0;
}
