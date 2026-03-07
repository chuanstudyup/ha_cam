#ifndef _UTILSFS_H_
#define _UTILSFS_H_

#include <stdio.h>
#include <stdbool.h>

#ifndef FILE_NAME_LEN
#define FILE_NAME_LEN 288
#endif

#define SD_MOUNT_POINT "/sdcard"

#define DATA_DIR "/data"
#define CONFIG_DIR "/config"

#define AVI_EXT "avi"
#define CSV_EXT "csv"
#define SRT_EXT "srt"

#define HTML_EXT ".htm"
#define TEXT_EXT ".txt"
#define JS_EXT ".js"
#define CSS_EXT ".css"
#define ICO_EXT ".ico"
#define SVG_EXT ".svg"
#define JPG_EXT ".jpg"
#define JSON_EXT ".json"

#define RAMSIZE (1024 * 8) // set this to multiple of SD card sector size (512 or 1024 bytes)
#define MIN_RAM 8 // min object size stored in ram instead of PSRAM default is 4096
#define MAX_RAM 4096 // max object size stored in ram instead of PSRAM default is 4096

#define INDEX_PAGE_PATH SD_MOUNT_POINT DATA_DIR "/MJPEG2SD" HTML_EXT
#define LOG_FILE_PATH DATA_DIR "/log" TEXT_EXT

/**
 * @brief 显示FAT文件系统信息
 * 
 * 获取并显示SD卡上FAT文件系统的详细信息，包括总簇数、空闲簇数、
 * 簇大小和扇区大小等信息。
 * 
 * @return true 获取文件系统信息成功
 * @return false 获取文件系统信息失败
 */
bool showFatFsInfo(void);

/**
 * @brief 获取SD卡总空间大小
 * 
 * 该函数通过FATFS文件系统API获取SD卡的总存储空间。
 * 使用f_getfree函数获取文件系统信息，然后计算总空间。
 * 
 * @return uint32_t 返回SD卡总空间大小，单位为KB。如果获取失败则返回0。
 */
uint32_t getSDTotalSpace(void);

/**
 * @brief 获取SD卡剩余空间
 * 
 * 该函数通过FATFS文件系统API获取SD卡剩余空间。
 * 
 * @return uint32_t 返回SD卡剩余空间大小，单位为KB。如果获取失败则返回0。
 */
uint32_t getSDFreeSpace(void);


/**
 * @brief 根据当前时间格式化日期字符串
 * 
 * 根据是否为文件夹类型，生成不同的日期格式路径字符串。
 * 如果是文件夹，格式为：挂载点/年月日
 * 如果是文件，格式为：挂载点/年月日/年月日_时分秒
 * 
 * @param inBuff 输出缓冲区，用于存储格式化后的字符串
 * @param inBuffLen 输出缓冲区的最大长度
 * @param isFolder 是否为文件夹格式（true为文件夹，false为文件）
 */
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);

/**
 * @brief 删除指定文件或文件夹
 * 
 * @param deleteThis 要删除的文件或文件夹路径
 * 
 * @note 该函数会检查是否为系统保留文件夹，如果是则不会删除
 * @note 对于文件夹会先清空内容再删除文件夹本身
 * @note 删除操作会记录日志信息
 */
void deleteFolderOrFile(const char *deleteThis);

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
bool checkFreeStorage();

/**
 * @brief 列出指定目录下的所有文件和子目录
 *
 * 遍历指定目录，递归列出所有文件和子目录，并显示文件大小信息。
 * 对于目录会标记为"Dir"，对于普通文件会显示文件大小，其他类型标记为"Other"。
 *
 * @param rootDir 要遍历的目录路径
 */
void listFolder(const char* rootDir);

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
bool listDir(const char *fname, char *jsonBuff, size_t jsonBuffLen, const char *extension);


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
void setFolderName(const char *fname, char *fileName);

#endif