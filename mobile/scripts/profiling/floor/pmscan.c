#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: pmscan <pid> <substr>\n");return 2;}
  char p[64]; snprintf(p,64,"/proc/%s/maps",argv[1]); FILE*f=fopen(p,"r"); if(!f){perror("maps");return 1;}
  snprintf(p,64,"/proc/%s/pagemap",argv[1]); int pm=open(p,O_RDONLY); if(pm<0){perror("pagemap");return 1;}
  char line[512]; unsigned long tot=0,res=0;
  while(fgets(line,512,f)){
    unsigned long a,b,off; char perm[8]; char path[400]={0};
    if(sscanf(line,"%lx-%lx %7s %lx %*s %*s %399s",&a,&b,perm,&off,path)<4) continue;
    if(!strstr(path,argv[2])) continue;
    unsigned long n=(b-a)/4096; uint64_t*e=malloc(n*8);
    if(pread(pm,e,n*8,(a/4096)*8)!=(ssize_t)(n*8)){perror("pread");return 1;}
    for(unsigned long i=0;i<n;i++){ tot++; if(e[i]>>63){res++; printf("%lu %c\n",(off/4096)+i,strchr(perm,'x')?'x':'-');} }
    free(e);
  }
  fprintf(stderr,"present %lu / %lu pages (%.1f MB)\n",res,tot,res*4.0/1024);
  return 0;
}
