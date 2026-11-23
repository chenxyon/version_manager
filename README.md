# Version Manager 组件

## 简介

Version Manager 组件用于管理 ESP-IDF 项目和组件的版本号，支持自动递增、手动指定、Git Tag 自动同步等多种模式。每个组件可维护独立的版本号，互不冲突。

## 功能特性

- **自动版本递增**：每次编译时自动递增补丁版本号
- **手动版本管理**：可手动指定主版本、次版本、补丁版本
- **Git Tag 同步**：自动读取当前 commit 的 git tag 作为版本号
- **版本冻结**：冻结模式下版本号固定，适合发布构建
- **独立组件版本**：每个组件可维护独立的版本号，互不冲突
- **上电自动打印**：芯片启动（含复位）时自动把所有已登记组件版本汇总打印到串口日志，无需在 `app_main` 里额外调用

## 版本优先级

版本号来源按以下优先级从高到低：

1. **Git Tag**：如果当前 commit 有精确匹配的 tag（如 `v1.0.0`），自动使用 tag 作为版本号
2. **Manual Mode**：通过 sdkconfig 手动指定版本号
3. **Auto Mode**：从 version.txt 读取并自动递增

---

## 核心设计：多项目/多组件版本互不冲突

### 设计原理

每个组件通过唯一的组件名称生成独立的宏定义前缀，确保不同组件的版本号宏不会冲突。

**关键代码**（[version_helper.cmake](cmake/version_helper.cmake#L11)）：

```cmake
function(register_component_version)
    set(oneValueArgs NAME VERSION_FILE OUTPUT_HEADER)
    ...
    string(TOUPPER ${ARG_NAME} COMPONENT_NAME_UPPER)
    string(REPLACE "-" "_" COMPONENT_NAME_UPPER ${COMPONENT_NAME_UPPER})
    ...
endfunction()
```

**宏命名规则**：`${COMPONENT_NAME_UPPER}_VERSION_MAJOR/MINOR/PATCH/STRING`

### 互不冲突示例

| 组件名称 | 宏前缀 | 头文件 | 版本号示例 |
|----------|--------|--------|-----------|
| version_manager | `VERSION_MANAGER_VERSION_*` | `version_manager_version.h` | `1.0.000` |
| font_service | `FONT_SERVICE_VERSION_*` | `font_service_version.h` | `1.3.000` |
| my_project | `MY_PROJECT_VERSION_*` | `my_project_version.h` | `2.0.001` |
| tft_ili9341 | `TFT_ILI9341_VERSION_*` | `tft_ili9341_version.h` | `1.2.000` |

这些宏定义完全独立，不会相互冲突。

---

## 上电自动打印所有组件版本

### 效果

芯片每次上电或复位，串口日志里会自动出现一张版本快照，**不需要在 `app_main` 里写任何代码**：

```
I (156) COMP_VER: ============ 已用组件版本（上电快照）============
I (156) COMP_VER:   COMPONENT              VERSION
I (156) COMP_VER:   ---------------------- -------
I (156) COMP_VER:   hc595_driver           v1.0.001
I (156) COMP_VER:   version_manager        v1.1.001
I (156) COMP_VER:   共 2 个组件
I (156) COMP_VER: ==============================================
```

### 组件侧怎么接入（一行）

在组件自己的 `.c` 文件里加两行即可：

```c
#include "component_version.h"
#include "hc595_driver_version.h"

/* 放在文件顶部、TAG 之后 */
COMPONENT_VERSION_REGISTER(hc595_driver, HC595_DRIVER_VERSION_STRING)
```

> **必须写在组件自己的 `.c` 里**，不要写在头文件里、也不要单独建一个 `.c`。
> 原因：ESP-IDF 组件会打成 `lib<name>.a` 静态库，链接器只取「被引用的目标文件」；
> 单独一个只含构造函数的 `.o` 会被丢弃，注册就不生效。
> 放在组件的业务源码里，该 `.o` 必被取出，构造函数才会进 `.init_array`。

### 实现原理

| 环节 | 优先级 | 说明 |
|------|--------|------|
| `COMPONENT_VERSION_REGISTER` | `constructor(200)` | 各组件在 `.init_array` 里把自己登记进全局表 |
| `component_version_dump_all` | `constructor(201)` | 等所有组件登记完，统一打印整表 |

- 优先级 200 < 201，保证「先全部登记，后统一打印」。
- GCC 保留优先级 0~100，用 `-Wall -Werror` 会报 `prio-ctor-dtor`，所以从 200 起算。
- `__libc_init_array()` 在 `do_core_init()`（含 `init_vfs_console`，CORE 阶段 priority 119）之后执行，
  因此构造函数里调用 `ESP_LOGI` 是安全的。
- 表按组件名去重，同名重复注册只覆盖版本号，不会产生重复行。

### API

| 函数 | 说明 |
|------|------|
| `void component_version_register(const char *name, const char *version)` | 登记一个组件版本（同名覆盖） |
| `void component_version_dump_all(void)` | 手动打印整张版本表 |
| `size_t component_version_get_count(void)` | 已登记组件数量 |
| `const component_version_info_t *component_version_get_entry(size_t i)` | 按下标取记录，越界返回 NULL |

表容量由 `COMPONENT_VERSION_MAX_ENTRIES`（默认 32）控制，超出会打 WARN。

---

## 使用方法

### 项目级版本管理

在项目根目录的 CMakeLists.txt 中注册项目版本：

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "version_manager"
)

idf_component_get_property(version_manager_PATH version_manager COMPONENT_DIR)
include(${version_manager_PATH}/cmake/version_helper.cmake)

register_component_version(NAME "my_project")
```

在代码中使用：

```c
#include "my_project_version.h"

printf("Project Version: %s\n", MY_PROJECT_VERSION_STRING);
printf("Major: %d, Minor: %d, Patch: %d\n", 
       MY_PROJECT_VERSION_MAJOR,
       MY_PROJECT_VERSION_MINOR,
       MY_PROJECT_VERSION_PATCH);
```

### 组件级版本管理

每个组件可以拥有独立的版本号，互不冲突。

**在 font_service 组件中的使用示例**：

```cmake
idf_component_register(
    SRCS "src/font_service.c"
    INCLUDE_DIRS "include"
    REQUIRES "version_manager"
)

idf_component_get_property(version_manager_PATH version_manager COMPONENT_DIR)
include(${version_manager_PATH}/cmake/version_helper.cmake)

register_component_version(NAME "font_service")
```

在代码中使用：

```c
#include "font_service_version.h"

printf("Font Service Version: %s\n", FONT_SERVICE_VERSION_STRING);
```

### register_component_version 参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| NAME | string | 是 | 组件/项目名称，用于生成头文件和宏名 |
| VERSION_FILE | string | 否 | 版本文件路径，默认 `${CMAKE_CURRENT_SOURCE_DIR}/version.txt` |
| OUTPUT_HEADER | string | 否 | 输出头文件路径，默认 `${CMAKE_CURRENT_SOURCE_DIR}/include/${NAME}_version.h` |
| AUTO_INCREMENT | flag | 否 | 启用自动递增（默认启用） |
| FREEZE | flag | 否 | 冻结版本号，不自动递增 |
| GIT_TAG | flag | 否 | 启用 Git Tag 自动检测 |

### 多组件版本示例

假设项目包含多个组件：

```
D:/esp/components/chenyong/
├── version_manager/
│   ├── CMakeLists.txt    → register_component_version(NAME "version_manager")
│   └── version.txt       → "1.0.000"
├── font_service/
│   ├── CMakeLists.txt    → register_component_version(NAME "font_service")
│   └── version.txt       → "1.3.000"
└── tft_ili9341/
    ├── CMakeLists.txt    → register_component_version(NAME "tft_ili9341")
    └── version.txt       → "1.2.000"
```

代码中使用：

```c
#include "version_manager_version.h"
#include "font_service_version.h"
#include "tft_ili9341_version.h"

printf("Version Manager: %s\n", VERSION_MANAGER_VERSION_STRING); // 1.0.000
printf("Font Service: %s\n", FONT_SERVICE_VERSION_STRING);       // 1.3.000
printf("TFT ILI9341: %s\n", TFT_ILI9341_VERSION_STRING);        // 1.2.000
```

### 使用 Git Tag 模式

启用 `GIT_TAG` 选项后，组件会自动读取当前 commit 的 tag：

```cmake
register_component_version(NAME my_component GIT_TAG)
```

当当前 commit 被标记为 `v1.2.003` 时，版本号自动设为 `1.2.003`。

### 使用 Freeze 模式

在发布构建时冻结版本号，避免不必要的递增：

```cmake
register_component_version(NAME my_component FREEZE)
```

---

## 新增组件版本管理步骤

### 步骤1：创建组件目录结构

```bash
mkdir -p D:/esp/components/chenyong/my_new_component/{include,src}
```

### 步骤2：创建 CMakeLists.txt

```cmake
idf_component_register(
    SRCS "src/my_new_component.c"
    INCLUDE_DIRS "include"
    REQUIRES "version_manager"
)

idf_component_get_property(version_manager_PATH version_manager COMPONENT_DIR)
include(${version_manager_PATH}/cmake/version_helper.cmake)

register_component_version(NAME "my_new_component")
```

### 步骤3：创建 version.txt

```bash
echo "1.0.000" > D:/esp/components/chenyong/my_new_component/version.txt
```

### 步骤4：创建版本头文件（可选，会自动生成）

构建时会自动生成 `include/my_new_component_version.h`：

```c
#ifndef MY_NEW_COMPONENT_VERSION_H
#define MY_NEW_COMPONENT_VERSION_H

#define MY_NEW_COMPONENT_VERSION_MAJOR    1
#define MY_NEW_COMPONENT_VERSION_MINOR    0
#define MY_NEW_COMPONENT_VERSION_PATCH    0
#define MY_NEW_COMPONENT_VERSION_STRING   "1.0.000"

#endif // MY_NEW_COMPONENT_VERSION_H
```

### 步骤5：在代码中使用版本号

```c
#include "my_new_component_version.h"

void my_new_component_init(void) {
    printf("My New Component Version: %s\n", MY_NEW_COMPONENT_VERSION_STRING);
}
```

### 步骤6：验证编译

```bash
cd your_project
idf.py build
```

---

## 全局永久规则

### 组件存储位置

所有自开发组件统一存放在：

```
D:/esp/components/chenyong/
```

### 组件命名规范

- 使用小写字母和下划线（如：`font_service`、`version_manager`）
- 避免使用中文和特殊字符

### 版本管理要求

1. **必须**在组件的 `CMakeLists.txt` 中集成 `version_manager`
2. **必须**调用 `register_component_version(NAME "组件名")` 注册版本
3. **必须**创建 `version.txt` 文件，初始版本号为 `1.0.000`
4. **必须**在组件的 `README.md` 中记录版本历史

### 文档更新要求

1. 修改代码时，**必须**同步更新组件的 `README.md`
2. 文档中**必须**包含：
   - 组件概述和特性
   - 接口说明和使用示例
   - 配置选项（如有）
   - 版本历史

---

## 发布工作流

### 标准发布流程

1. **开发阶段**：使用 Auto Mode，版本自动递增
2. **准备发布**：
   ```bash
   echo "1.0.000" > version.txt
   ```
3. **确认版本**：构建验证版本正确
4. **打标签**：
   ```bash
   git add version.txt
   git commit -m "Release version 1.0.000"
   git tag v1.0.000
   git push origin v1.0.000
   ```
5. **继续开发**：下一次构建自动从 1.0.000 递增到 1.0.001

### 使用 Git Tag 模式

启用 `GIT_TAG` 选项后，组件会自动读取当前 commit 的 tag：

```cmake
register_component_version(NAME my_component GIT_TAG)
```

当当前 commit 被标记为 `v1.2.003` 时，版本号自动设为 `1.2.003`。

### 使用 Freeze 模式

在发布构建时冻结版本号，避免不必要的递增：

```cmake
register_component_version(NAME my_component FREEZE)
```

---

## 版本格式

- 自动模式：`主版本.次版本.补丁版本` (例如：`1.0.001`)
- Git Tag 模式：直接使用 tag 值（去除 `v` 前缀）

---

## 文件结构

```
version_manager/
├── CMakeLists.txt
├── Kconfig
├── cmake/
│   └── version_helper.cmake    # 核心版本管理函数
├── include/
│   ├── component_version.h                  # 组件版本注册表 API + 自注册宏
│   └── version_manager_version.h            # version_manager 自身版本（自动生成）
├── src/
│   └── version_manager.c       # 注册表实现 + 上电自动打印
├── version.txt                 # 当前版本号
└── version_template.h.in       # 版本头文件模板
```

---

## 生成的宏定义

每个组件会生成以下宏定义：

| 宏名 | 说明 | 示例 |
|------|------|------|
| `${NAME}_VERSION_MAJOR` | 主版本号 | `MY_COMPONENT_VERSION_MAJOR` |
| `${NAME}_VERSION_MINOR` | 次版本号 | `MY_COMPONENT_VERSION_MINOR` |
| `${NAME}_VERSION_PATCH` | 补丁版本号 | `MY_COMPONENT_VERSION_PATCH` |
| `${NAME}_VERSION_STRING` | 版本字符串 | `"1.0.001"` |

---

## 依赖关系

- 无外部依赖（纯 CMake 实现）

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0.0 | 2026-07-11 | 初始版本，支持自动递增和 Git Tag 模式 |
| v1.1.0 | 2026-07-11 | 优化多组件版本管理，每个组件独立版本号 |
| v1.2.0 | 2026-07-12 | 添加全局组件开发规则，完善多项目/多组件版本互不冲突说明 |
| v1.1.001 | 2026-11-08 | **新增组件版本注册表**：`component_version.h` 提供 `COMPONENT_VERSION_REGISTER` 宏，芯片上电/复位时通过 `constructor(200/201)` 自动汇总打印所有已用组件版本；新增 `component_version_register/dump_all/get_count/get_entry` API |
