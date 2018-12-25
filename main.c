#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// DBR的一些属性
struct DosBootRecord
{
    uint16_t BytesPerSector;
    uint8_t SectorPerCluster;
    uint16_t ReservedSectorCount;
    uint8_t FATsCount;
    uint32_t TotalSectorCount;
    uint32_t FATSize;
} DBR;

// FAT32的一些参数
struct FileAllocationTable32
{
    uint32_t FAT1Offset;
    uint32_t FAT2Offset;
    uint32_t RootDirectoryOffset;
    uint32_t BytesPerCluster;
    uint32_t DirItemPerCluster;
} FAT32;

// 存储目录项的结构体
typedef struct DirectoryInfo
{
    uint8_t LongDirOrder;
    uint8_t *Name;
    uint8_t Attribute;
    uint32_t Cluster;
    uint32_t Size;
} DirectoryInfo;

// 从文件中读取一个字节
uint8_t getByteFromFile(FILE *input, uint32_t offset, uint32_t whence)
{
    uint8_t n;
    fseek(input, offset, whence);
    n = fgetc(input);
    return n;
}

// 从文件中读取一个字
uint16_t getWordFromFile(FILE *input, uint32_t offset, uint32_t whence)
{
    uint16_t n;
    fseek(input, offset, whence);
    n = fgetc(input);
    n += fgetc(input) << 8;
    return n;
}

// 从文件中读取一个双字
uint32_t getDoubleWordFromFile(FILE *input, uint32_t offset, uint32_t whence)
{
    uint32_t n;
    fseek(input, offset, whence);
    n = fgetc(input);
    n += fgetc(input) << 8;
    n += fgetc(input) << 16;
    n += fgetc(input) << 24;
    return n;
}

// 从字符串中读取一个字节
uint8_t getByte(uint8_t *buffer, uint32_t offset)
{
    uint8_t n;
    buffer += offset;
    n = buffer[0];
    return n;
}

// 从字符串中读取一个字
uint16_t getWord(uint8_t *buffer, uint32_t offset)
{
    uint16_t n;
    buffer += offset;
    n = buffer[0];
    n += buffer[1] << 8;
    return n;
}

// 从字符串中读取一个双字
uint32_t getDoubleWord(uint8_t *buffer, uint32_t offset)
{
    uint32_t n;
    buffer += offset;
    n = buffer[0];
    n += buffer[1] << 8;
    n += buffer[2] << 16;
    n += buffer[3] << 24;
    return n;
}

// 初始化DBR的相关属性
void initDBR(FILE *input, struct DosBootRecord *dbr)
{
    dbr->BytesPerSector = getWordFromFile(input, 11, SEEK_SET);
    dbr->SectorPerCluster = getByteFromFile(input, 13, SEEK_SET);
    dbr->ReservedSectorCount = getWordFromFile(input, 14, SEEK_SET);
    dbr->FATsCount = getByteFromFile(input, 16, SEEK_SET);
    dbr->TotalSectorCount = getDoubleWordFromFile(input, 32, SEEK_SET);
    dbr->FATSize = getDoubleWordFromFile(input, 36, SEEK_SET);

}

// 初始化FAT32的相关参数
void initFAT32(struct FileAllocationTable32 *fat32, struct DosBootRecord *dbr)
{
    fat32->FAT1Offset = dbr->BytesPerSector * dbr->ReservedSectorCount;
    fat32->FAT2Offset = fat32->FAT1Offset + dbr->BytesPerSector * dbr->FATSize;
    fat32->RootDirectoryOffset = fat32->FAT2Offset + dbr->BytesPerSector * dbr->FATSize;
    fat32->BytesPerCluster = dbr->SectorPerCluster * dbr->BytesPerSector;
    fat32->DirItemPerCluster = fat32->BytesPerCluster / 0x20;
}

// 从文件中读取一个簇，返回一个指向buffer的指针
uint8_t *getCluster(FILE *input, uint32_t number)
{
    uint32_t offset;
    uint8_t *buffer;
    offset = FAT32.RootDirectoryOffset + FAT32.BytesPerCluster * (number - 2);
    buffer = (uint8_t *) calloc(FAT32.BytesPerCluster, 1);
    fseek(input, offset, SEEK_SET);
    fread(buffer, 1, FAT32.BytesPerCluster, input);
    return buffer;
}

// 十六进制输出buffer
void printBuffer(uint8_t *buffer, uint32_t size)
{
    for (int i = 0; i < size; i++)
    {
        printf("%02X ", buffer[i]);
        if (i % 4 == 3)
        {
            printf(" ");
        }
        if (i % 16 == 15)
        {
            printf("\n");
        }
    }
}

// 从buffer中读取一个目录项
DirectoryInfo *getDirectoryItem(uint8_t *buffer, uint32_t offset)
{
    uint8_t *directoryItem, attribute, *name;
    directoryItem = buffer + offset;
    attribute = directoryItem[0xB];
    DirectoryInfo *directoryInfo = calloc(1, sizeof(DirectoryInfo));
    // 判断目录项种类
    if (attribute == 0xF)
    {
        // 长目录项
        directoryInfo->LongDirOrder = directoryItem[0x0];
        name = calloc(14, sizeof(uint8_t));
        int count = 0;
        for (count = 0; count < 5; count++)
        {
            name[count] = directoryItem[1 + count * 2];
        }
        for (count = 5; count < 11; count++)
        {
            name[count] = directoryItem[4 + count * 2];
        }
        name[11] = directoryItem[28];
        name[12] = directoryItem[30];
        directoryInfo->Name = name;
        directoryInfo->Attribute = attribute;
        directoryInfo->Cluster = 0;
        directoryInfo->Size = 0;
    } else
    {
        // 短目录项
        directoryInfo->LongDirOrder = 0;
        directoryInfo->Attribute = attribute;
        directoryInfo->Size = getDoubleWord(directoryItem, 0x1C);
        uint32_t temp = getWord(directoryItem, 0x14) << 16;
        temp += getWord(directoryItem, 0x1A);
        directoryInfo->Cluster = temp;
        name = calloc(12, sizeof(uint8_t));
        for (int count = 0; count < 11; count++)
        {
            name[count] = directoryItem[count];
        }
        directoryInfo->Name = name;
    }
    return directoryInfo;
}

// 返回FAT32表中的下一簇簇号
uint32_t getNextCluster(FILE *input, uint32_t cluster)
{
    return getDoubleWordFromFile(input, FAT32.FAT1Offset + cluster * 4, SEEK_SET);
}

// 输入第一簇的簇号，返回该目录的目录项结构体指针数组
DirectoryInfo **getRawDirectory(FILE *input, uint32_t cluster)
{
    uint8_t isEnd = 0;
    uint8_t *buffer = getCluster(input, cluster);
    uint32_t size = 0;
    uint32_t nextCluster;
    DirectoryInfo **directoryList = calloc(FAT32.DirItemPerCluster, sizeof(DirectoryInfo *));
    DirectoryInfo *temp;
    while (1)
    {
        for (int count = 0; count < FAT32.DirItemPerCluster; count++)
        {
            // 获取一个目录项结构体
            temp = getDirectoryItem(buffer, count * 0x20);
            if (temp->LongDirOrder == 0 && temp->Name[0] == 0)
            {
                // 遍历到达结束
                isEnd = 1;
                break;
            } else if (temp->Name[0] == 0xe5 || temp->LongDirOrder == 0xe5)
            {
                // 该条目录已被删除
                continue;
            } else
            {
                // 合法的目录项
                directoryList[size++] = temp;
            }
        }
        if (isEnd)
        {
            free(buffer);
            break;
        }
        // 检索下一簇
        nextCluster = getNextCluster(input, cluster);
        if (nextCluster >= 0xf8)
        {
            // 到达结束
            free(buffer);
            break;
        } else if (nextCluster == 0xf7)
        {
            // 坏簇
            free(buffer);
            break;
        } else if (nextCluster >= 0xf0)
        {
            // 系统保留
            free(buffer);
            break;
        } else if (nextCluster >= 0x02)
        {
            // 已分配
            free(buffer);
            cluster = nextCluster;
            buffer = getCluster(input, cluster);
            directoryList = realloc(directoryList, (size + FAT32.DirItemPerCluster) * sizeof(DirectoryInfo *));
        } else
        {
            // 错误
        }
    }
    directoryList = realloc(directoryList, (size + 1) * sizeof(DirectoryInfo *));
    directoryList[size] = NULL;
    return directoryList;
}

// 显示目录的深度
void showDepth(uint32_t depth)
{
    for (int count = 0; count < depth; count++)
    {
        printf("    ");
    }
    printf("├── ");
}

// 递归遍历目录并输出
void walkDirectory(FILE *input, uint32_t cluster, uint32_t depth)
{
    // 获取目录项列表
    DirectoryInfo **directoryList = getRawDirectory(input, cluster);
    DirectoryInfo *directoryItem;
    uint32_t count = 0;
    uint8_t attribute, isLongName, *tempName;
    isLongName = 0;
    // 若不是根目录则跳过前两个目录项
    if (directoryList[0]->Name[0] == '.' && directoryList[0]->Name[1] == ' ')
    {
        count = 2;
    }
    // 遍历目录项
    while (directoryList[count] != NULL)
    {
        directoryItem = directoryList[count++];
        attribute = directoryItem->Attribute;
        if (attribute == 0xf)
        {
            // 长目录项
            if (isLongName)
            {
                // 非首个长目录项，将两次名称合并
                uint32_t length = strlen(tempName) + strlen(directoryItem->Name) + 1;
                uint32_t stringSize = 0;
                uint8_t *newName = calloc(length, sizeof(uint8_t));
                uint8_t *tempPointer = directoryItem->Name;
                while (*tempPointer != 0)
                {
                    newName[stringSize++] = *tempPointer++;
                }
                tempPointer = tempName;
                while (*tempPointer != 0)
                {
                    newName[stringSize++] = *tempPointer++;
                }
                free(tempName);
                tempName = newName;
            } else
            {
                // 首个长目录项
                isLongName = 1;
                uint32_t length = strlen(directoryItem->Name) + 1;
                uint8_t *newName = calloc(length, sizeof(uint8_t));
                strcpy(newName, directoryItem->Name);
                tempName = newName;
            }

        } else if (attribute & 0x8)
        {
            // 卷标
            printf("Volume ID: %s\n", directoryItem->Name);
            isLongName = 0;
        } else if (attribute & 0x10)
        {
            // 子目录
            showDepth(depth);
            if (isLongName)
            {
                printf("\033[;34m%s\033[0m\n", tempName);
                isLongName = 0;
            } else
            {
                printf("\033[;34m%s\033[0m\n", directoryItem->Name);
            }
            walkDirectory(input, directoryItem->Cluster, depth + 1);
        } else if (attribute & 0x20)
        {
            // 文件
            showDepth(depth);
            if (isLongName)
            {
                printf("%s", tempName);
                isLongName = 0;
            } else
            {
                for (int i = 0; i < 8; i++)
                {
                    if (directoryItem->Name[i] == ' ')
                    {
                        break;
                    }
                    putchar(directoryItem->Name[i]);
                }
                putchar('.');
                for (int i = 8; i < 11; i++)
                {
                    if (directoryItem->Name[i] == ' ')
                    {
                        break;
                    }
                    putchar(directoryItem->Name[i]);
                }
            }
            printf("\033[;32m  %d Bytes\033[0m\n", directoryItem->Size);
        }
    }
}

int main()
{
    FILE *input;
    input = fopen("/Users/hanlinghao/CLionProjects/fat32/fat32d.img", "rb");
    initDBR(input, &DBR);
    initFAT32(&FAT32, &DBR);
    // 递归遍历根目录
    walkDirectory(input, 0x2, 0);
    return 0;
}