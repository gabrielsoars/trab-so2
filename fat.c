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
    if (!mountState)
        return -1;  // não está montado
        
    // Verifica se o nome não é muito longo
    if (strlen(name) > MAX_LETTERS)
        return -1;
        
    // Verifica se o arquivo já existe
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) {
            return -1;  // arquivo já existe
        }
    }
    
    // Procura entrada livre no diretório
    int free_entry = -1;
    for (int i = 0; i < N_ITEMS; i++) {
        if (!dir[i].used) {
            free_entry = i;
            break;
        }
    }
    
    if (free_entry == -1)
        return -1;  // diretório cheio
        
    // Cria a entrada no diretório (arquivo vazio)
    dir[free_entry].used = 1;
    strcpy(dir[free_entry].name, name);
    dir[free_entry].length = 0;
    dir[free_entry].first = 0;  // arquivo vazio não tem blocos
    
    // Salva o diretório no disco
    ds_write(DIR, (char *)dir);
    
    return 0;
}

int fat_delete( char *name){
    if (!mountState)
        return -1;
        
    // Procura o arquivo no diretório
    int file_entry = -1;
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) {
            file_entry = i;
            break;
        }
    }
    
    if (file_entry == -1)
        return -1;  // arquivo não encontrado
        
    // Libera os blocos do arquivo na FAT
    unsigned int blk = dir[file_entry].first;
    while (blk != 0 && blk < sb.number_blocks) {
        unsigned int next_blk = fat[blk];
        fat[blk] = FREE;
        if (next_blk == EOFF)
            break;
        blk = next_blk;
        // Proteção contra loops infinitos
        if (blk == dir[file_entry].first) break;
    }
    
    // Remove a entrada do diretório
    dir[file_entry].used = 0;
    memset(dir[file_entry].name, 0, MAX_LETTERS+1);
    dir[file_entry].length = 0;
    dir[file_entry].first = 0;
    
    // Salva as mudanças no disco
    ds_write(DIR, (char *)dir);
    
    // Salva a FAT no disco
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(TABLE + i, (char *)(fat + i * (BLOCK_SIZE / sizeof(int))));
    }
    
    return 0;
}

int fat_getsize( char *name){ 
    if (!mountState)
        return -1;
        
    // Procura o arquivo no diretório
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) {
            return dir[i].length;
        }
    }
    
    return -1;  // arquivo não encontrado
}

// Função auxiliar para encontrar um bloco livre
static int find_free_block() {
    for (int i = TABLE + sb.n_fat_blocks; i < sb.number_blocks; i++) {
        if (fat[i] == FREE) {
            return i;
        }
    }
    return -1; // disco cheio
}

int fat_read( char *name, char *buff, int length, int offset){
    if (!mountState)
        return -1;
        
    // Procura o arquivo no diretório
    int file_entry = -1;
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) {
            file_entry = i;
            break;
        }
    }
    
    if (file_entry == -1)
        return -1;  // arquivo não encontrado
        
    // Verifica se o offset está dentro do arquivo
    if (offset >= dir[file_entry].length)
        return 0;  // offset além do fim do arquivo
        
    // Ajusta o length para não ler além do arquivo
    if (offset + length > dir[file_entry].length)
        length = dir[file_entry].length - offset;
        
    if (length <= 0)
        return 0;
        
    // Se arquivo vazio
    if (dir[file_entry].first == 0)
        return 0;
        
    int bytes_read = 0;
    unsigned int current_block = dir[file_entry].first;
    int block_offset = offset;
    char block_buffer[BLOCK_SIZE];
    
    // Pula blocos até chegar ao offset
    while (block_offset >= BLOCK_SIZE && current_block != 0) {
        block_offset -= BLOCK_SIZE;
        if (fat[current_block] == EOFF)
            return 0;
        current_block = fat[current_block];
    }
    
    // Lê os dados
    while (length > 0 && current_block != 0 && current_block < sb.number_blocks) {
        ds_read(current_block, block_buffer);
        
        int bytes_to_copy = BLOCK_SIZE - block_offset;
        if (bytes_to_copy > length)
            bytes_to_copy = length;
            
        memcpy(buff + bytes_read, block_buffer + block_offset, bytes_to_copy);
        
        bytes_read += bytes_to_copy;
        length -= bytes_to_copy;
        block_offset = 0; // nos próximos blocos, começamos do início
        
        if (fat[current_block] == EOFF)
            break;
        current_block = fat[current_block];
    }
    
    return bytes_read;
}

int fat_write( char *name, const char *buff, int length, int offset){
    if (!mountState)
        return -1;
        
    // Procura o arquivo no diretório
    int file_entry = -1;
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) {
            file_entry = i;
            break;
        }
    }
    
    if (file_entry == -1)
        return -1;  // arquivo não encontrado
        
    // Se o arquivo está vazio e offset > 0, não podemos escrever
    if (dir[file_entry].first == 0 && offset > 0)
        return -1;
        
    // Se arquivo vazio, aloca primeiro bloco
    if (dir[file_entry].first == 0) {
        int new_block = find_free_block();
        if (new_block == -1)
            return -1; // disco cheio
            
        fat[new_block] = EOFF;
        dir[file_entry].first = new_block;
    }
    
    int bytes_written = 0;
    unsigned int current_block = dir[file_entry].first;
    int block_offset = offset;
    char block_buffer[BLOCK_SIZE];
    
    // Navega até o bloco correto baseado no offset
    while (block_offset >= BLOCK_SIZE && current_block != 0) {
        block_offset -= BLOCK_SIZE;
        if (fat[current_block] == EOFF) {
            // Precisa alocar novo bloco
            int new_block = find_free_block();
            if (new_block == -1)
                return bytes_written; // disco cheio
            fat[current_block] = new_block;
            fat[new_block] = EOFF;
            current_block = new_block;
        } else {
            current_block = fat[current_block];
        }
    }
    
    // Escreve os dados
    while (length > 0 && current_block != 0) {
        // Lê o bloco atual
        ds_read(current_block, block_buffer);
        
        int bytes_to_write = BLOCK_SIZE - block_offset;
        if (bytes_to_write > length)
            bytes_to_write = length;
            
        memcpy(block_buffer + block_offset, buff + bytes_written, bytes_to_write);
        
        // Escreve o bloco de volta
        ds_write(current_block, block_buffer);
        
        bytes_written += bytes_to_write;
        length -= bytes_to_write;
        block_offset = 0; // próximos blocos começam do início
        
        if (length > 0) {
            if (fat[current_block] == EOFF) {
                // Precisa alocar novo bloco
                int new_block = find_free_block();
                if (new_block == -1)
                    break; // disco cheio
                fat[current_block] = new_block;
                fat[new_block] = EOFF;
                current_block = new_block;
            } else {
                current_block = fat[current_block];
            }
        }
    }
    
    // Atualiza o tamanho do arquivo se necessário
    if (offset + bytes_written > dir[file_entry].length) {
        dir[file_entry].length = offset + bytes_written;
    }
    
    // Salva o diretório e a FAT
    ds_write(DIR, (char *)dir);
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(TABLE + i, (char *)(fat + i * (BLOCK_SIZE / sizeof(int))));
    }
    
    return bytes_written;
}