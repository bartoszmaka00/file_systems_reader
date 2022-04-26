#include <stdint.h>
#ifndef INC_2_3PROJEKT_FILE_READER_H
#define INC_2_3PROJEKT_FILE_READER_H
struct disk_t{
    uint8_t  jump_code[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_dir_capacity;
    uint16_t logical_sectors16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t chs_sectors_per_track;
    uint16_t chs_tracks_per_cylinder;
    uint32_t hidden_sectors;
    uint32_t logical_sectors32;
    uint8_t  media_id;
    uint8_t  chs_head;
    uint8_t  ext_bpb_signature;
    uint32_t serial_number;
    char     volume_label[11];
    char     fsid[8];
    uint8_t  boot_code[448];
    uint16_t magic;
    FILE *file;
}__attribute__ (( packed ));
struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);


typedef uint32_t sector_t;
typedef uint32_t cluster_t;
typedef uint16_t fat_date_t;
typedef uint16_t fat_time_t;



struct volume_t
{
    char*bufor;
    struct disk_t *pdisk;
    sector_t volume_start;
    sector_t fat1_position;
    sector_t fat2_position;
    sector_t rootdir_position;
    sector_t rootdir_size;
    sector_t cluster2_position;
    sector_t wolumin_size;
    sector_t user_space;
    cluster_t total_clusters;
    uint8_t *fat1;
    uint8_t *fat2;
    uint16_t *data_fat;

};
struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);

struct file_t
{
    struct volume_t *pvolume;
    uint32_t file_size;
    cluster_t nr_cluster;
    uint16_t nr_byte;
    cluster_t current;
    uint16_t start;
};

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);

//Otwieranie, czytanie i zamykanie katalogów
struct dir_entry_t {
    char name[13];
    uint32_t size;
    uint8_t is_archived;
    uint8_t is_readonly;
    uint8_t is_system;
    uint8_t is_hidden;
    uint8_t is_directory;
}__attribute__(( packed ));

struct root_directory{
    uint8_t charFileName[11];
    union {
        uint8_t result;
        struct {
            uint8_t is_readonly : 1;
            uint8_t is_hidden : 1;
            uint8_t is_system : 1;
            uint8_t volume_label : 1;
            uint8_t is_directory : 1;
            uint8_t is_archived : 1;
            uint8_t unused : 1;
        };
    }fileAttribute;
    uint8_t reserved;
    uint8_t fileCreationTimeSeconds;
    union {
        uint16_t result;
        struct {
            uint16_t seconds: 5;
            uint16_t minutes: 6;
            uint16_t hours: 5;
        };
    }creationTimeHMS;

    union {
        uint16_t result;
        struct {
            uint16_t day: 5;
            uint16_t month: 4;
            uint16_t year: 7;
        };
    }creationDateYMD;

    uint16_t accessDate;
    uint16_t HOaddressFirstCluster;

    union {
        uint16_t total;
        struct {
            uint16_t seconds: 5;
            uint16_t minutes: 6;
            uint16_t hours: 5;
        };
    }modificationTime;

    union {
        uint16_t result;
        struct {
            uint16_t day: 5;
            uint16_t month: 4;
            uint16_t year: 7;
        };
    }modificationDate;
    uint16_t LOaddressFirstCluster;
    uint32_t fileSize;
}__attribute__ (( packed ));

struct dir_t{
    struct root_directory *wpisy;
    int current;
    int size;
};
struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);


#endif //INC_2_3PROJEKT_FILE_READER_H
