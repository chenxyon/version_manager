/**
 * @file     version_manager.cpp
 * @brief    组件版本注册表实现
 *
 * 功能：维护全局组件版本表，提供登记、查询与上电自动打印；所有登记通过
 *       COMPONENT_VERSION_REGISTER 宏在 .init_array 中完成，version_manager 只用
 *       constructor(201) 在所有组件登记完毕后把整表打到串口日志
 * 修改：2026-11-21 由 version_manager.c 重构为 C++ class
 */
#include "version_manager.h"

#include <string.h>

#include "esp_log.h"

#include "version_manager_version.h"

static const char *TAG = "COMP_VER";

VersionManager &VersionManager::instance()
{
    static VersionManager inst;
    return inst;
}

void VersionManager::registerComponent(const char *name, const char *version)
{
    if (name == nullptr || version == nullptr) {
        return;
    }

    /* 同名覆盖，避免多文件重复注册导致重复行 */
    for (size_t i = 0; i < m_count; i++) {
        if (strcmp(m_entries[i].name, name) == 0) {
            m_entries[i].version = version;
            return;
        }
    }

    if (m_count >= MAX_ENTRIES) {
        ESP_LOGW(TAG, "版本表已满（容量 %u），丢弃 %s",
                 (unsigned)MAX_ENTRIES, name);
        return;
    }

    m_entries[m_count].name = name;
    m_entries[m_count].version = version;
    m_count++;
}

void VersionManager::dumpAll() const
{
    ESP_LOGI(TAG, "============ 已用组件版本（上电快照）============");
    ESP_LOGI(TAG, "  %-22s %s", "COMPONENT", "VERSION");
    ESP_LOGI(TAG, "  %-22s %s", "----------------------", "-------");
    for (size_t i = 0; i < m_count; i++) {
        ESP_LOGI(TAG, "  %-22s v%s", m_entries[i].name, m_entries[i].version);
    }
    ESP_LOGI(TAG, "  共 %u 个组件", (unsigned)m_count);
    ESP_LOGI(TAG, "==============================================");
}

const ComponentVersionInfo *VersionManager::entry(size_t index) const
{
    if (index >= m_count) {
        return nullptr;
    }
    return &m_entries[index];
}

/* 组件自身版本也登记（优先级 200，与其它组件同批） */
COMPONENT_VERSION_REGISTER(version_manager, VERSION_MANAGER_VERSION_STRING)

/* 所有组件登记完成后再统一打印（优先级 201 > 200） */
static void __attribute__((constructor(VersionManager::DUMP_PRIO)))
componentVersionBootDump(void)
{
    VersionManager::instance().dumpAll();
}
