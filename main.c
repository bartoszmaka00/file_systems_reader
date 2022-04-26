#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "file_reader.h"
#include <stdint.h>
#include "tested_declarations.h"
#include "rdebug.h"
#define BYTES_PER_SECTOR 512
int main() {

   return 0;
}

//Otwieranie, czytanie i zamykanie urządzenia blokowego (w formie pliku)

struct disk_t* disk_open_from_file(const char* volume_file_name)
{
    if(volume_file_name==NULL){
        errno=EFAULT;
        return NULL;
    }
    struct disk_t* p=(struct disk_t*)malloc(sizeof(struct disk_t));
    if(!p){
        errno=ENOMEM;
        return NULL;
    }
    p->file=fopen(volume_file_name,"rb");
    if(!p->file){
        errno=ENOENT;
        free(p);
        return NULL;
    }
    return p;
}
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read)
{
    if(!pdisk || !buffer){
        errno=EFAULT;
        return -1;
    }
    fseek(pdisk->file,first_sector*BYTES_PER_SECTOR,SEEK_SET);
    int result=(int)fread(buffer,BYTES_PER_SECTOR,sectors_to_read,pdisk->file);
    if(result != sectors_to_read){
        errno=ERANGE;
        return -1;
    }
    return result;
}
int disk_close(struct disk_t* pdisk)
{
    if(!pdisk){
        errno=EFAULT;
        return -1;
    }
    fclose(pdisk->file);
    free(pdisk);
    return 0;
}

//Otwieranie i zamykanie woluminu FAT12

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector)
{
    if(!pdisk){
        errno=EFAULT;
        return NULL;
    }
    int test=disk_read(pdisk,first_sector,pdisk,1);
    int spc=pdisk->sectors_per_cluster;
    if(spc!=1 && spc!=2 && spc!=4 && spc!=8 && spc!=16 && spc!=32&& spc!=64&& spc!=128){
        errno=EINVAL;
        return NULL;
    }
    if(test==-1 || pdisk->reserved_sectors<=0 || (pdisk->fat_count!=1 && pdisk->fat_count!=2)
    || (pdisk->root_dir_capacity* sizeof(struct root_directory))%(int)pdisk->bytes_per_sector!=0){
        errno=EINVAL;
        return NULL;
    }
    struct volume_t*p=(struct volume_t*)malloc(sizeof(struct volume_t));
    if(!p){
        errno=ENOMEM;
        return NULL;
    }
    p->pdisk=pdisk;
    p->volume_start=0;
    p->fat1_position=p->volume_start+pdisk->reserved_sectors;
    p->fat2_position=p->fat1_position+pdisk->sectors_per_fat;
    p->rootdir_position=p->fat1_position+2*pdisk->sectors_per_fat;
    p->rootdir_size=(pdisk->root_dir_capacity* sizeof(struct root_directory))/(int)pdisk->bytes_per_sector;
    if((pdisk->root_dir_capacity* sizeof(struct root_directory))%(int)pdisk->bytes_per_sector!=0){
        p->rootdir_size+=1;
    }
    p->cluster2_position=p->rootdir_position+p->rootdir_size;
    p->wolumin_size=pdisk->logical_sectors16==0?pdisk->logical_sectors32 : pdisk->logical_sectors16;
    p->user_space=p->wolumin_size - pdisk->reserved_sectors - pdisk->fat_count * pdisk->sectors_per_fat-p->rootdir_size;
    p->total_clusters=p->user_space/pdisk->sectors_per_cluster;

    p->bufor=(char*)malloc(p->wolumin_size*pdisk->bytes_per_sector);
    if(!p->bufor){
        errno=ENOMEM;
        return NULL;
    }
    disk_read(pdisk,0,p->bufor,p->wolumin_size);

    p->fat1=(uint8_t*)malloc(pdisk->sectors_per_fat*pdisk->bytes_per_sector);
    p->fat2=(uint8_t*)malloc(pdisk->sectors_per_fat*pdisk->bytes_per_sector);
    if(!p->fat1 || !p->fat2){
        errno=ENOMEM;
        return NULL;
    }
    memcpy(p->fat1,p->bufor+(p->fat1_position*pdisk->bytes_per_sector),pdisk->sectors_per_fat*pdisk->bytes_per_sector);
    memcpy(p->fat2,p->bufor+(p->fat2_position*pdisk->bytes_per_sector),pdisk->sectors_per_fat*pdisk->bytes_per_sector);

    p->data_fat=(uint16_t*)malloc(sizeof(uint16_t)*p->total_clusters);
    if(!p->data_fat){
        errno=ENOMEM;
        return NULL;
    }
    for(int i=0,j=0;j<(int)p->total_clusters;i+=3,j+=2)
    {
        uint8_t b0=p->fat1[i+0];
        uint8_t b1=p->fat1[i+1];
        uint8_t b2=p->fat1[i+2];

        uint16_t byte1=((uint16_t)(b1 & 0x0F)<< 8) | b0;
        uint16_t byte2=((uint16_t)b2<<4) | ((b1 & 0xF0)>>4);

        p->data_fat[j+0]=byte1;
        p->data_fat[j+1]=byte2;
    }
    return p;
}
int fat_close(struct volume_t* pvolume)
{
    if(!pvolume){
        errno=EFAULT;
        return -1;
    }
    free(pvolume->fat1);
    free(pvolume->fat2);
    free(pvolume->data_fat);
    free(pvolume->bufor);
    free(pvolume);
    return 0;
}

//Otwieranie, przeszukiwanie, czytanie oraz zamykanie plików w systemie FAT

struct file_t* file_open(struct volume_t* pvolume, const char* file_name)
{
    if(!pvolume){
        errno=EFAULT;
        return NULL;
    }
    if(!file_name){
        errno=ENOENT;
        return NULL;
    }
    struct root_directory *dir=(struct root_directory *)malloc(pvolume->rootdir_size*pvolume->pdisk->bytes_per_sector);
    if(!dir){
        errno=ENOMEM;
        return NULL;
    }
    struct root_directory *dirfree=dir;

    memcpy(dir,pvolume->bufor+pvolume->rootdir_position*pvolume->pdisk->bytes_per_sector,pvolume->rootdir_size*pvolume->pdisk->bytes_per_sector);

    char name[13];
    int jest=0;

    //sprawdzanie wpisów
    for(int k=0;k<(int)(BYTES_PER_SECTOR/sizeof(struct root_directory));k++){
        int i;
        for(i=0;i<8;i++){
            if(dir->charFileName[i]==' ')break;
            name[i]=dir->charFileName[i];
        }
        for(int j=8;j<11;j++,i++)
        {
            if(dir->charFileName[j]==' ')break;
            if(j==8){
                name[i]='.';
                i++;
            }
            name[i]=dir->charFileName[j];
        }
        name[i]='\0';
        if(strcmp(file_name,name)==0 && (dir->fileAttribute.is_directory==1 || dir->fileAttribute.volume_label==1)){
            free(dirfree);
            errno=EISDIR;
            return NULL;
        }
        if(strcmp(file_name,name)==0){
            jest=1;
            break;
        }
        dir++;
    }

    if(jest==0){
        free(dirfree);
        errno=ENOENT;
        return NULL;
    }
    struct file_t *fp=(struct file_t *)malloc(sizeof(struct file_t));
    if(!fp){
        free(dirfree);
        errno=ENOMEM;
        return NULL;
    }
    fp->pvolume=pvolume;
    fp->file_size=dir->fileSize;
    fp->nr_byte=0;
    fp->nr_cluster=0;
    fp->start=dir->LOaddressFirstCluster;
    fp->current=fp->start;
    free(dirfree);
    return fp;
}
int file_close(struct file_t* stream)
{
    if(!stream){
        errno=EFAULT;
        return -1;
    }
    free(stream);
    return 0;
}
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (!ptr || !stream) {
        errno = EFAULT;
        return -1;
    }
    if (stream->file_size == stream->nr_byte)return 0;
    int bytes_in_cluster = stream->pvolume->pdisk->sectors_per_cluster * stream->pvolume->pdisk->bytes_per_sector;
    intptr_t offset = 0;
    uint32_t pos,move = 0,rest=0;
    size_t licz = 0;

    while (1) {
        sector_t start_cluster = stream->pvolume->cluster2_position + ((stream->current - 2) * stream->pvolume->pdisk->sectors_per_cluster);
        pos = start_cluster * stream->pvolume->pdisk->bytes_per_sector;
        rest = stream->file_size - stream->nr_byte;
        if (stream->current == stream->pvolume->data_fat[1]) break;
        if((int) (size * nmemb) <= bytes_in_cluster)
        {
            move = stream->nr_byte - (stream->nr_cluster * bytes_in_cluster);
            unsigned int k = bytes_in_cluster - (stream->nr_byte - stream->nr_cluster * bytes_in_cluster);
            if ((int) (size * nmemb) > (int)rest)
            {
                memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos + move, rest);

                licz += rest / size;
                offset = rest;
                file_seek(stream, offset, SEEK_CUR);
                break;
            }
            if (size * nmemb > k)
            {
                memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos + move, k);
                file_seek(stream, k, SEEK_CUR);
                offset += k;
                start_cluster = stream->pvolume->cluster2_position +((stream->current - 2) * stream->pvolume->pdisk->sectors_per_cluster);
                pos = start_cluster * stream->pvolume->pdisk->bytes_per_sector;
                memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos, (size * nmemb) - k);
                offset = (size * nmemb) - k;
                licz += nmemb;
                file_seek(stream, (size * nmemb) - k, SEEK_CUR);
                break;
            }
            memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos + move, size * nmemb);
            offset = size * nmemb;
            licz += nmemb;
            file_seek(stream, offset, SEEK_CUR);
            break;
        }
        else{
            if (bytes_in_cluster > (int)rest)
            {
                memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos, rest);
                offset += rest;
                file_seek(stream, offset, SEEK_SET);
                licz += rest / size;
                break;
            }
            memcpy((char *) ptr + offset, (char *) stream->pvolume->bufor + pos, bytes_in_cluster);
            offset += bytes_in_cluster;
            file_seek(stream, offset, SEEK_SET);
            licz += bytes_in_cluster / size;
        }
    }
    return licz;
}

int32_t file_seek(struct file_t* stream, int32_t offset, int whence)
{
   if(!stream){
       errno=EFAULT;
       return -1;
   }
   if(whence<0 || whence > (int)stream->file_size){
       errno = EINVAL;
       return -1;
   }
   if(whence==SEEK_SET) stream->nr_byte=offset;
   else if(whence==SEEK_CUR)stream->nr_byte+=offset;
   else if(whence==SEEK_END)stream->nr_byte=stream->file_size+offset;
   else{
       errno=ENXIO;
       return -1;
   }
   stream->nr_cluster=stream->nr_byte/(stream->pvolume->pdisk->sectors_per_cluster*stream->pvolume->pdisk->bytes_per_sector);
   stream->current=stream->start;
   for(int i=0;i<(int)stream->nr_cluster;++i){
       stream->current=stream->pvolume->data_fat[stream->current];
   }
   return stream->nr_byte;
}


struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path)//do poprawy
{
    if(!pvolume || !dir_path){
        errno=EFAULT;
        return NULL;
    }
    if(dir_path==NULL || strcmp(dir_path,"\\")!=0){
        errno=ENOENT;
        return NULL;
    }
    struct dir_t*result=(struct dir_t*)malloc(sizeof(struct dir_t));
    if(!result){
        errno=ENOMEM;
        return NULL;
    }
    result->wpisy=(struct root_directory*)malloc(pvolume->rootdir_size*pvolume->pdisk->bytes_per_sector);
    if(!result->wpisy){
        errno=ENOMEM;
        return NULL;
    }
    memcpy(result->wpisy,pvolume->bufor+(pvolume->rootdir_position*pvolume->pdisk->bytes_per_sector),pvolume->rootdir_size*pvolume->pdisk->bytes_per_sector);
    result->current=0;
    result->size=pvolume->pdisk->root_dir_capacity;
    return result;
}
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry)
{
    if(!pentry){
        errno=EFAULT;
        return -1;
    }
    if(!pdir){
        errno=EIO;
        return -1;
    }
    if(pdir->current>=pdir->size)return 1;

    int i;
    while(1)
    {
        if(pdir->wpisy[pdir->current].charFileName[0]==0xe5 || pdir->wpisy[pdir->current].charFileName[0]==(char)0x00){
            pdir->current++;
            if(pdir->current>=pdir->size)return 1;
            continue;
        }
        for(i=0;i<8;i++){
            if(pdir->wpisy[pdir->current].charFileName[i]==' ')break;
            pentry->name[i]=pdir->wpisy[pdir->current].charFileName[i];
        }
        for(int j=8;j<11;j++,i++)
        {
            if(pdir->wpisy[pdir->current].charFileName[j]==' ')break;
            if(j==8){
                pentry->name[i]='.';
                i++;
            }
            pentry->name[i]=pdir->wpisy[pdir->current].charFileName[j];
        }
        pentry->name[i]='\0';
        pentry->size=pdir->wpisy[pdir->current].fileSize;
        pentry->is_directory=pdir->wpisy[pdir->current].fileAttribute.is_directory;
        pentry->is_hidden=pdir->wpisy[pdir->current].fileAttribute.is_hidden;
        pentry->is_archived=pdir->wpisy[pdir->current].fileAttribute.is_archived;
        pentry->is_readonly=pdir->wpisy[pdir->current].fileAttribute.is_readonly;
        pentry->is_system=pdir->wpisy[pdir->current].fileAttribute.is_system;

        pdir->current++;
        return 0;
    }


}
int dir_close(struct dir_t* pdir)
{
    if(!pdir){
        errno=EFAULT;
        return -1;
    }
    free(pdir->wpisy);
    free(pdir);
    return 0;
}
