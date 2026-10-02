#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: pagescan drop|scan <file>\n");return 2;}
  int fd=open(argv[2],O_RDONLY); if(fd<0){perror("open");return 1;}
  struct stat st; fstat(fd,&st);
  if(!strcmp(argv[1],"drop")){ int r=posix_fadvise(fd,0,0,POSIX_FADV_DONTNEED); printf("fadvise rc=%d\n",r); return 0; }
  size_t pg=4096, n=(st.st_size+pg-1)/pg;
  void*m=mmap(0,st.st_size,PROT_READ,MAP_SHARED,fd,0); if(m==MAP_FAILED){perror("mmap");return 1;}
  unsigned char*v=malloc(n); if(mincore(m,st.st_size,v)){perror("mincore");return 1;}
  size_t res=0; for(size_t i=0;i<n;i++) if(v[i]&1){res++; printf("%zu\n",i);}
  fprintf(stderr,"resident %zu / %zu pages (%.1f MB)\n",res,n,res*4.0/1024);
  return 0;
}
