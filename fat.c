#include "fat.h"
#include "ds.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SUPER 0
#define TABLE 2
#define DIR 1

#define SIZE 1024

// the superblock
#define MAGIC_N           0xAC0010DE
typedef struct{
	int magic;
	int number_blocks;
	int n_fat_blocks;
	char empty[BLOCK_SIZE-3*sizeof(int)];
} super;

super sb;

//item
#define MAX_LETTERS 6
#define OK 1
#define NON_OK 0
typedef struct{
	unsigned char used;
	char name[MAX_LETTERS+1];
	unsigned int length;
	unsigned int first;
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item))
dir_item dir[N_ITEMS];

// table
#define FREE 0
#define EOFF 1
#define BUSY 2
unsigned int *fat;

int mountState = 0;

int fat_format(){ 
    if (mountState)
        return -1;  // não pode formatar se já estiver montado

    // Inicializa o superbloco
    sb.magic = MAGIC_N;
    sb.number_blocks = ds_size();
    sb.n_fat_blocks = (int) ceil((double)(sb.number_blocks * sizeof(int)) / BLOCK_SIZE);

    // Grava o superbloco no disco
    ds_write(SUPER, (char *)&sb);

    // Inicializa o diretório: tudo vazio
    memset(dir, 0, sizeof(dir));
    ds_write(DIR, (char *)dir);

    // Aloca e inicializa a FAT
    fat = (unsigned int*) malloc(sb.number_blocks * sizeof(int));
    if (!fat)
        return -1;

    // Marca todos os blocos como livres inicialmente
    for (int i = 0; i < sb.number_blocks; i++)
        fat[i] = FREE;

    // Reservar blocos especiais (superbloco, diretório e FAT)
    fat[SUPER] = BUSY;
    fat[DIR] = BUSY;
    for (int i = 0; i < sb.n_fat_blocks; i++)
        fat[TABLE + i] = BUSY;

    // Grava a FAT no disco
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(TABLE + i, (char *)(fat + i * (BLOCK_SIZE / sizeof(int))));
    }

    // Libera a FAT temporária (não está montado ainda)
    free(fat);

    return 0;
}

void fat_debug() {
    // Lê o superbloco
    ds_read(SUPER, (char *)&sb);
    printf("superblock:\n");
    if (sb.magic == MAGIC_N)
        printf("magic is ok\n");
    else
        printf("magic is WRONG (0x%x)\n", sb.magic);
    printf("%d blocks\n", sb.number_blocks);
    printf("%d block fat\n", sb.n_fat_blocks);

    // Lê o diretório
    ds_read(DIR, (char *)dir);

    // Aloca e lê a FAT
    fat = (unsigned int*) malloc(sb.number_blocks * sizeof(int));
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_read(TABLE + i, (char *)(fat + i * (BLOCK_SIZE / sizeof(int))));
    }

    // Percorre o diretório
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used) {
            printf("File \"%s\":\n", dir[i].name);
            printf("  size: %u bytes\n", dir[i].length);
            printf("  Blocks:");
            unsigned int blk = dir[i].first;
            while (blk != 0 && blk != EOFF) {
                printf(" %u", blk);
                if (fat[blk] == EOFF)
                    break;
                blk = fat[blk];
            }
            printf("\n");
        }
    }

    free(fat);
}

int fat_mount(){
  	ds_read(SUPER, (char *)&sb);

    if (sb.magic != MAGIC_N)
        return -1;

    ds_read(DIR, (char *) dir);

    fat = (unsigned int *)malloc(sb.number_blocks * sizeof(unsigned int));
    if (!fat)
        return -1;

    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_read(TABLE + i, (char *)(fat + i * (BLOCK_SIZE / sizeof(int))));
    }

    mountState = 1;

    return 0;
}

int fat_create(char *name){
  	return 0;
}

int fat_delete( char *name){
  	return 0;
}

int fat_getsize( char *name){ 
	return 0;
}

//Retorna a quantidade de caracteres lidos
int fat_read( char *name, char *buff, int length, int offset){
	return 0;
}

//Retorna a quantidade de caracteres escritos
int fat_write( char *name, const char *buff, int length, int offset){
	return 0;
}
