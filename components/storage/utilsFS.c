#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

#include "esp_vfs_fat.h"
#include "esp_log.h"

#include "Utils.h"
#include "utilsFS.h"

#define TAG "UtilsFS"

int sdFreeSpaceMode;          // 0 - No Check, 1 - Delete oldest dir, 2 - Upload to ftp and then delete folder on SD
int sdMinCardFreeSpace = 100; // Minimum amount of card free Megabytes before sdFreeSpaceMode action is enabled

static char *resetDir = "/~reset";
static char *currentDir = "/~current";
static char *previousDir = "/~previous";

/**
 * @brief 获取最旧的目录
 *
 * 遍历SD卡挂载点下的所有目录，排除系统目录和数据目录，
 * 通过比较目录名称找到最旧的目录（按字母顺序最小的目录名）。
 *
 * @param oldestDir 输出参数，用于存储找到的最旧目录完整路径
 *                  （格式：挂载点/目录名）
 *
 * @return true  成功找到最旧目录
 * @return false 未找到符合条件的目录或打开目录失败
 */
static bool getOldestDir(char *oldestDir)
{
  bool find = false;
  DIR *dir = NULL;
  struct dirent *entry;

  dir = opendir(SD_MOUNT_POINT);
  if (dir == NULL)
  {
    ESP_LOGE(TAG, "Unable to open directory %s", SD_MOUNT_POINT);
    return false;
  }

  while ((entry = readdir(dir)) != NULL)
  {
    if (entry->d_type != DT_DIR)
    {
      continue;
    }

    if (strstr(entry->d_name, "System") != NULL || strstr(entry->d_name, DATA_DIR) != NULL || strstr(entry->d_name, "SYSTEM") != NULL)
    {
      continue;
    }

    // 比较文件夹名称，找到最旧的目录
    if (strlen(oldestDir) == 0 || strcmp(oldestDir, entry->d_name) > 0)
    {
      strcpy(oldestDir, entry->d_name);
      snprintf(oldestDir, FILE_NAME_LEN, "%s/%s", SD_MOUNT_POINT, entry->d_name);
      find = true;
    }
  }
  closedir(dir);
  return find;
}

/**
 * @brief 根据当前时间格式化日期字符串
 *
 * 根据是否为文件夹类型，生成不同的日期格式路径字符串。
 * 如果是文件夹，格式为：挂载点/年月日/
 * 如果是文件，格式为：挂载点/年月日/年月日_时分秒
 *
 * @param inBuff 输出缓冲区，用于存储格式化后的字符串
 * @param inBuffLen 输出缓冲区的最大长度
 * @param isFolder 是否为文件夹格式（true为文件夹，false为文件）
 */
void dateFormat(char *inBuff, size_t inBuffLen, bool isFolder)
{
  // construct filename from date/time
  time_t currEpoch = time(NULL);
  char tmp_buf[32] = {0};
  if (isFolder)
    strftime(tmp_buf, sizeof(tmp_buf), "%Y%m%d/", localtime(&currEpoch));
  else
    strftime(tmp_buf, sizeof(tmp_buf), "%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));

  snprintf(inBuff, inBuffLen, "%s/%s", SD_MOUNT_POINT, tmp_buf);
}

/**
 * 设置文件夹名称
 *
 * 根据输入的文件名参数设置当前或前一天的文件夹名称。
 * 支持特殊标记"~"来表示当前目录或前一天目录。
 *
 * @param fname     输入的文件名字符串，支持特殊标记：
 *                  - "~current": 设置当前日期格式的目录
 *                  - "~previous": 设置前一天日期格式的目录
 * @param fileName  输出参数，用于存储生成的文件夹名称, 带有挂载点路径前缀
 */
void setFolderName(const char *fname, char *fileName)
{
  // set current or previous folder
  char partName[FILE_NAME_LEN] = {0};
  bool hasMountPoint = (strstr(fname, SD_MOUNT_POINT) == NULL) ? false : true;

  if (strchr(fname, '~') != NULL)
  {
    if (!strcmp(fname, currentDir))
    {
      dateFormat(partName, sizeof(partName), true);
      strcpy(fileName, partName);
      ESP_LOGI(TAG, "Current directory set to %s", fileName);
    }
    else if (!strcmp(fname, previousDir))
    {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      struct tm *tm = localtime(&tv.tv_sec);
      tm->tm_mday -= 1;
      time_t prev = mktime(tm);
      strftime(partName, sizeof(partName), SD_MOUNT_POINT "/%Y%m%d/", localtime(&prev));
      strcpy(fileName, partName);
      ESP_LOGI(TAG, "Previous directory set to %s", fileName);
    }
    else
    {
      sprintf(fileName, "%s/", SD_MOUNT_POINT);
    }
  }
  else
  {
    if (hasMountPoint)
    {
      strcpy(fileName, fname);
    }
    else
    {
      sprintf(fileName, "%s%s", SD_MOUNT_POINT, fname);
    }
  }
}

/**
 * @brief 修改文件扩展名
 *
 * 将原始文件扩展名替换为指定的新扩展名。函数会查找文件名中的最后一个点号('.')，
 * 并将其后的内容替换为新扩展名。
 *
 * @note 调用者需要确保文件名缓冲区足够大以容纳新扩展名
 *
 * @param fileName 要修改的文件名字符串（必须可写且缓冲区足够大）
 * @param newExt 新的扩展名字符串（不含点号，如"txt"、"jpg"等）
 *
 * @return bool 修改是否成功
 *         - true: 成功找到扩展名位置并完成修改
 *         - false: 未找到扩展名位置（文件名中无点号）
 */
static bool changeExtension(char *fileName, const char *newExt)
{
  // replace original file extension with supplied extension (buffer must be large enough)
  size_t inNamePtr = strlen(fileName);
  // find '.' before extension text
  while (inNamePtr > 0 && fileName[inNamePtr] != '.')
    inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(fileName + inNamePtr, newExt, extLen);
  fileName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

/**
 * @brief 删除与基础文件相关的其他格式文件
 *
 * 根据给定的基础文件名，删除对应的CSV和SRT格式文件（如果存在）。
 * 主要用于清理与视频/数据文件相关的辅助文件。
 *
 * @param baseFile 基础文件名（不包含扩展名或包含任意扩展名）
 * @note 函数会修改基础文件名的扩展名来查找对应的CSV和SRT文件
 * @note 使用FILE_NAME_LEN长度的缓冲区存储修改后的文件名
 */
static void deleteOthers(const char *baseFile)
{
#ifdef ISCAM
  // delete corresponding csv and srt files if exist
  char otherDeleteName[FILE_NAME_LEN];
  strcpy(otherDeleteName, baseFile);
  changeExtension(otherDeleteName, CSV_EXT);
  if (remove(otherDeleteName))
    ESP_LOGI(TAG, "File %s deleted", otherDeleteName);
  changeExtension(otherDeleteName, SRT_EXT);
  if (remove(otherDeleteName))
    ESP_LOGI(TAG, "File %s deleted", otherDeleteName);
#endif
}

/**
 * @brief 删除指定文件或文件夹
 *
 * @param deleteThis 要删除的文件或文件夹路径
 *
 * @note 该函数会检查是否为系统保留文件夹，如果是则不会删除
 * @note 对于文件夹会先清空内容再删除文件夹本身
 * @note 删除操作会记录日志信息
 */
void deleteFolderOrFile(const char *deleteThis)
{
  // delete supplied file or folder, unless it is a reserved folder
  char fileName[FILE_NAME_LEN] = {0};
  setFolderName(deleteThis, fileName);
  struct stat st;
  stat(fileName, &st);

  if (S_ISREG(st.st_mode))
  {
    // is file
    ESP_LOGI(TAG, "File %s size %s %sdeleted", deleteThis, fmtSize(st.st_size), remove(deleteThis) ? "" : "not "); // Remove the file
    deleteOthers(deleteThis);
  }
  else if (S_ISDIR(st.st_mode))
  {
    // is folder
    if (strstr(fileName, "System") != NULL || strstr(fileName, DATA_DIR) != NULL)
    {
      ESP_LOGW(TAG, "Deletion of %s not permitted", fileName);
      vTaskDelay(500); // reduce thrashing on same error
      return;
    }

    struct dirent *entry;
    DIR *dir = opendir(fileName);
    if (dir == NULL)
    {
      ESP_LOGW(TAG, "Failed to open directory %s", fileName);
      return;
    }

    int hasFolder = 0;
    while ((entry = readdir(dir)) != NULL)
    {
      char deleteFilepath[FILE_NAME_LEN];
      strcpy(deleteFilepath, fileName);
      strcat(deleteFilepath, "/");
      strcat(deleteFilepath, entry->d_name);
      stat(deleteFilepath, &st);
      if (S_ISREG(st.st_mode))
      {
        ESP_LOGI(TAG, "FILE : %s Size : %s %sdeleted", deleteFilepath, fmtSize(st.st_size), remove(deleteFilepath) ? "not " : "");
        deleteOthers(deleteFilepath);
      }
      else if (S_ISDIR(st.st_mode))
      {
        ESP_LOGI(TAG, "DIR : %s", deleteFilepath);
        hasFolder++;
      }
    }
    closedir(dir);

    if (!hasFolder)
    {
      ESP_LOGI(TAG, "Folder %s %sdeleted", fileName, rmdir(fileName) ? "not " : ""); // Remove the folder
    }
  }
  else
  {
    ESP_LOGW(TAG, "Unknown file type %s. Don't delete", fileName);
  }
}

/**
 * 检查存储空间并确保有足够的空闲空间
 *
 * 函数会检查SD卡的总空间，如果空闲空间小于设定的最小值，会根据配置模式进行处理：
 * - 普通模式：仅记录警告日志
 * - 清理模式：删除最旧的文件夹来释放空间
 * - 传输后清理模式：先传输文件夹内容再删除
 *
 * @return bool 空间检查结果
 *         - true: 空间充足或已成功清理出足够空间
 *         - false: 空间不足且未启用清理模式
 */
bool checkFreeStorage()
{
  // Check for sufficient space on storage
  bool res = false;
  size_t freeSize = getSDTotalSpace() / 1024; // in MB
  if (!sdFreeSpaceMode && freeSize < sdMinCardFreeSpace)
    ESP_LOGW(TAG, "Space left %uMB is less than minimum %uMB", freeSize, sdMinCardFreeSpace);
  else
  {
    // delete to make space
    while (freeSize < sdMinCardFreeSpace)
    {
      char oldestDir[FILE_NAME_LEN];
      getOldestDir(oldestDir);
      ESP_LOGW(TAG, "Deleting oldest folder: %s %s", oldestDir, sdFreeSpaceMode == 2 ? "after uploading" : "");
#if INCLUDE_FTP_HFS
      if (sdFreeSpaceMode == 2)
        fsStartTransfer(oldestDir); // transfer and then delete oldest folder
#endif
      deleteFolderOrFile(oldestDir);
      freeSize = getSDTotalSpace() / 1024;
    }
    ESP_LOGI(TAG, "Storage free space: %lu MB", getSDTotalSpace() / 1024);
    res = true;
  }
  return res;
}


/**
 * @brief 列出指定目录下的所有文件和子目录
 *
 * 遍历指定目录，递归列出所有文件和子目录，并显示文件大小信息。
 * 对于目录会标记为"Dir"，对于普通文件会显示文件大小，其他类型标记为"Other"。
 *
 * @param rootDir 要遍历的目录路径
 */
void listFolder(const char *rootDir)
{
  // list contents of folder
  DIR *dir = opendir(rootDir);
  int count = 0;
  struct dirent *entry;
  struct stat st;
  char fullPath[FILE_NAME_LEN];

  if (dir == NULL)
  {
    ESP_LOGE(TAG, "Failed to open directory %s", rootDir);
    return;
  }

  ESP_LOGI(TAG, "Listing folder: %s", rootDir);
  while ((entry = readdir(dir)) != NULL)
  {
    memset(fullPath, 0, FILE_NAME_LEN);
    if (entry->d_type == DT_DIR)
    {
      ESP_LOGI(TAG, "%d, Dir: %s", count++, entry->d_name);
    }
    else if (entry->d_type == DT_REG)
    {
      snprintf(fullPath, FILE_NAME_LEN, "%s/%s", rootDir, entry->d_name);
      stat(fullPath, &st);
      if (S_ISREG(st.st_mode))
      {
        ESP_LOGI(TAG, "%d, File: %s, size: %s", count++, fullPath, fmtSize(st.st_size));
      }
    }
    else
    {
      ESP_LOGI(TAG, "%d, Other: %s", count++, entry->d_name);
    }
  }
  closedir(dir);
}

/**
 * @brief 列出目录内容并生成JSON格式的列表
 *
 * 根据输入路径列出目录内容，生成JSON格式的字符串用于前端展示。
 * 可以列出根目录下的日期文件夹或指定日期文件夹内的文件。
 *
 * @param fname 输入路径，支持特殊标记：
 *              - "~current": 当前日期目录
 *              - "~previous": 前一天日期目录
 *              - 其他: 直接指定的目录路径
 * @param jsonBuff 输出缓冲区，用于存储生成的JSON字符串
 * @param jsonBuffLen 输出缓冲区的最大长度
 * @param extension 文件扩展名过滤器，仅列出匹配的文件
 *
 * @return bool 返回是否匹配到指定扩展名的文件
 *         - true: 输入路径直接匹配了指定扩展名
 *         - false: 输入路径未匹配扩展名(执行了目录列表)
 */
bool listDir(const char *fname, char *jsonBuff, size_t jsonBuffLen, const char *extension)
{
  // either list day folders in root, or files in a day folder
  bool hasExtension = false;
  char partJson[700]; // used to build SD page json buffer
  char fileName[128];
  char upPath[128];
  char fullPath[388];
  struct stat st;
  char *ptr = NULL;

  if (0 == strcmp(fname, resetDir))
  {
    sprintf(jsonBuff, "{\"/\":\"List folders\",\"%s\":\"Go to current (today)\",\"%s\":\"Go to previous (yesterday)\"}", currentDir, previousDir);
    return hasExtension;
  }

  setFolderName(fname, fileName); // full path with mount point
  ESP_LOGI(TAG, "fname = %s, fileName = %s, extension = %s", fname, fileName, extension);

  // check if folder or file
  if (strstr(fileName, extension) != NULL || strstr(fileName, JSON_EXT))
  {
    // required file type selected
    hasExtension = true;
    strcpy(jsonBuff, "{}");
    return hasExtension;
  }
  else
  {
    DIR *root = opendir(fileName);
    if (!root)
    {
      ESP_LOGW(TAG, "Failed to open directory %s", fileName);
      sprintf(jsonBuff, "{\"/\":\"List folders\",\"%s\":\"Go to current (today)\",\"%s\":\"Go to previous (yesterday)\"}", currentDir, previousDir);
      return hasExtension;
    }

    bool returnDirs = false;
    if (strlen(fname) > 1)
    {
      strcpy(upPath, fileName);
      char * p_tmp = strrchr(upPath, '/');
      if (p_tmp != NULL)
      {
        *p_tmp = '\0'; // remove trailing '/' if present
        p_tmp = strrchr(upPath, '/'); // find last '/' again
        if (p_tmp != NULL)
        {
          *(p_tmp+1) = '\0'; // remove trailing '/' if present
          returnDirs = true; // return dirs only
        }
      }
    }
    ESP_LOGI(TAG, "returnDirs = %d", returnDirs);
    // build relevant option list
    if (returnDirs)
    {
      sprintf(partJson, "{\"%s\":\".. [ Up ]\",", upPath + strlen(SD_MOUNT_POINT));
      strcpy(jsonBuff, partJson);
    }
    else
    {
      strcpy(jsonBuff,"{");
    }

    bool needFilter = true;
    if (strstr(fname, CONFIG_DIR) != NULL)
    {
      needFilter = false;
    }

    struct dirent *entry;
    while ((entry = readdir(root)) != NULL)
    {
      if (entry->d_type == DT_DIR && strstr(entry->d_name, DATA_DIR) == NULL)
      {
        // build folder list, ignore data folder
        ptr = fileName + strlen(SD_MOUNT_POINT);
        sprintf(partJson, "\"%s%s/\":\"%s\",", ptr, entry->d_name, entry->d_name);
        strcat(jsonBuff, partJson);
      }
      if (entry->d_type == DT_REG)
      {
        // build file list
        if (!needFilter || strstr(entry->d_name, extension) != NULL)
        {
          sprintf(fullPath, "%s/%s", fileName, entry->d_name);
          stat(fullPath, &st);
          ptr = fileName + strlen(SD_MOUNT_POINT);
          sprintf(partJson, "\"%s%s\":\"%s %s\",", ptr, entry->d_name, entry->d_name, fmtSize(st.st_size));
          strcat(jsonBuff, partJson);
        }
      }
    }
    closedir(root);
  }
  jsonBuff[strlen(jsonBuff) - 1] = '}'; // lose trailing comma

  return hasExtension;
}

/**
 * @brief 显示FAT文件系统信息
 *
 * 获取并显示SD卡上FAT文件系统的详细信息，包括总簇数、空闲簇数、
 * 簇大小和扇区大小等信息。
 *
 * @return true 获取文件系统信息成功
 * @return false 获取文件系统信息失败
 */
bool showFatFsInfo(void)
{
  FATFS *fs;
  DWORD fre_clust;
  FRESULT fr = f_getfree("/sdcard", &fre_clust, &fs);
  if (fr != FR_OK)
  {
    ESP_LOGE(TAG, "f_getfree return %d", fr);
    return false;
  }

  // n_fatent: total number of clusters in the FS
  // csize: number of sectors per cluster
  // ssize: number of bytes per sector
  // fre_clust: number of free clusters
  ESP_LOGI(TAG, "Total clusters: %lu. Free clusters: %lu. Cluster size: %u. Sector size: %u",
           fs->n_fatent, fre_clust, fs->csize, fs->ssize);

  uint32_t total = (fs->csize * fs->n_fatent * fs->ssize); // in B
  uint32_t free = (fs->csize * fre_clust * fs->ssize);     // in B
  ESP_LOGI(TAG, "SD Total space: %s. SD Free space: %s", fmtSize(total), fmtSize(free));

  return true;
}

/**
 * @brief 获取SD卡总空间大小
 *
 * 该函数通过FATFS文件系统API获取SD卡的总存储空间。
 * 使用f_getfree函数获取文件系统信息，然后计算总空间。
 *
 * @return uint32_t 返回SD卡总空间大小，单位为KB。如果获取失败则返回0。
 */
uint32_t getSDTotalSpace(void)
{
  FATFS *fs;
  DWORD fre_clust;
  FRESULT fr = f_getfree("/sdcard", &fre_clust, &fs);
  if (fr != FR_OK)
  {
    return 0;
  }
  uint32_t total = (fs->csize * fs->n_fatent * fs->ssize) / 1024; // in KB

  return total;
}

/**
 * @brief 获取SD卡剩余空间
 *
 * 该函数通过FATFS文件系统API获取SD卡剩余空间。
 *
 * @return uint32_t 返回SD卡剩余空间大小，单位为KB。如果获取失败则返回0。
 */
uint32_t getSDFreeSpace(void)
{
  FATFS *fs;
  DWORD fre_clust;
  FRESULT fr = f_getfree("/sdcard", &fre_clust, &fs);
  if (fr != FR_OK)
  {
    return 0;
  }
  uint32_t free = (fs->csize * fre_clust * fs->ssize) / 1024; // in KB
  return free;
}