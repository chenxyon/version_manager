/**
 * @file     version_manager.h
 * @brief    组件版本注册表 —— 上电/复位时统一打印所有已用组件的版本（C++ class）
 *
 * 功能：提供一套「组件自注册 + 启动自动汇总打印」的机制。
 *       每个组件只需在自己的 .cpp 里写一行：
 *
 *           #include "version_manager.h"
 *           COMPONENT_VERSION_REGISTER(hc595_driver, HC595_DRIVER_VERSION_STRING)
 *
 *       该行会在 .init_array 中注册一个 constructor(200)，在芯片启动阶段
 *       （__libc_init_array() 执行时）把自己登记进 VersionManager 单例；
 *       version_manager 再用 constructor(201) 把整张表打到串口日志。
 *       优先级 200 < 201 保证「先全部登记，后统一打印」。
 *
 * @note     为什么用 constructor 而不是在 app_main 里调用：
 *           1) 不侵入业务代码，任何工程接上 version_manager 就自动生效；
 *           2) __libc_init_array() 在 do_core_init() 之后执行，
 *              构造函数里调用 ESP_LOGI 是安全的；
 *           3) 宏放在组件自己的 .cpp 里，保证该目标文件一定会被链接器取出。
 *
 * 修改：2026-11-21 由 component_version.h（C API）重构为 C++ class
 */
#ifndef VERSION_MANAGER_H
#define VERSION_MANAGER_H

#include <stddef.h>

/**
 * 单条版本记录
 */
struct ComponentVersionInfo {
    const char *name;    /* 组件名，取自注册宏的第一个参数 */
    const char *version; /* 版本号字符串，如 "1.0.107" */
};

class VersionManager {
public:
    /* 注册表容量：全局表最多容纳多少个组件，超出打 WARN 并丢弃 */
    static constexpr size_t MAX_ENTRIES = 32;

    /* constructor 优先级：数字小的先执行（避开 0~100，GCC 保留给实现） */
    static constexpr int REGISTER_PRIO = 200;
    static constexpr int DUMP_PRIO     = 201;

    /* 全局唯一实例 */
    static VersionManager &instance();

    /**
     * 登记一个组件版本
     * 按组件名去重写入；重复登记覆盖版本号而不是产生两条
     */
    void registerComponent(const char *name, const char *version);

    /* 以表格形式打印全部已登记组件版本到串口日志 */
    void dumpAll() const;

    /* 查询当前已登记的组件数量 */
    size_t count() const { return m_count; }

    /**
     * 按下标取一条版本记录
     * @param index 下标，范围 [0, count())
     * @return 记录指针；越界返回 nullptr
     */
    const ComponentVersionInfo *entry(size_t index) const;

private:
    VersionManager() = default;

    ComponentVersionInfo m_entries[MAX_ENTRIES] = {};
    size_t m_count = 0;
};

/**
 * 组件版本自注册宏
 * 在 .init_array 中注册一个 constructor，芯片启动时自动登记版本
 *
 * @param tag     组件名（不加引号，宏会做字符串化），同时用作静态符号名后缀
 * @param ver_str 版本号宏，如 HC595_DRIVER_VERSION_STRING
 * @param prio    constructor 优先级
 */
#define COMPONENT_VERSION_REGISTER_AT(tag, ver_str, prio)                \
    static void __attribute__((constructor(prio))) _cvr_ctor_##tag(void) \
    {                                                                    \
        VersionManager::instance().registerComponent(#tag, ver_str);     \
    }

#define COMPONENT_VERSION_REGISTER(tag, ver_str) \
    COMPONENT_VERSION_REGISTER_AT(tag, ver_str, VersionManager::REGISTER_PRIO)

#endif /* VERSION_MANAGER_H */
