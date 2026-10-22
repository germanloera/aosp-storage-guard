# Storage Guard Subsystem for AOSP

## 1. Introduction

### Project Overview & Educational Purpose

> **Note:** This project is a personal practical demonstration and hands-on learning laboratory for the **Android Open Source Project (AOSP)**. It was designed and built to explore native platform internals, low-level service lifecycle management via `init`, inter-process communication (IPC) through UNIX domain sockets, Soong blueprint configuration, and SELinux domain containment in Android systems.

### What is Storage Guard?

**Storage Guard** is a native system-level telemetry and monitoring service designed for the Android Open Source Project (AOSP). It consists of an init-managed background daemon (`storage_guard`), a shared telemetry calculation library (`libstorage_telemetry`), and a command-line client utility (`storage_guard_ctl`).

### What does it do & What is it for?

In Android platform engineering and embedded OS runtimes, running out of filesystem storage on key partitions like `/data` (user data) or `/system` (OS binaries) can lead to boot loops, silent crash states, or failing write transactions. `storage_guard` demonstrates an end-to-end platform design to address this by:

- Continuously exposing an Android UNIX domain socket interface managed by the platform `init` system.

- Querying filesystem allocation geometry (blocks, block size, available space) via low-level kernel filesystem APIs (`statvfs`).

- Evaluating partition thresholds dynamically to categorize health status into `HEALTHY`, `WARNING`, or `CRITICAL`.

- Providing structured JSON telemetry back to clients over IPC for platform services, system health monitors, or CLI debug utilities.

## 2. Architecture & How It Works

Storage Guard adopts standard Android system service primitives:

```
+-----------------------------------------------------------+
| Android init (/system/etc/init/storage_guard.rc)          |
| - Manages daemon lifecycle                                |
| - Creates /dev/socket/storage_guard (0666 system system)  |
+-----------------------------+-----------------------------+
                              | Socket descriptor passing
                              v
+-----------------------------------------------------------+
| storage_guard (Daemon - /system/bin/storage_guard)        |
| - Inherits socket via android_get_control_socket()        |
| - Listens and accepts client connections                  |
| - Calls libstorage_telemetry::StorageCollector            |
+-----------------------------+-----------------------------+
       ^                      | Links against
       | IPC (UNIX Domain)    v
       |               +------------------------------------+
       |               | libstorage_telemetry.so            |
       |               | - Performs statvfs() queries       |
       |               | - Formats JSON telemetry response  |
       |               +------------------------------------+
+------+----------------------+
| storage_guard_ctl (Client)  |
| - Connects to socket        |
| - Sends commands (e.g. CHECK)|
| - Prints JSON to stdout     |
+-----------------------------+

```

1. **Socket Allocation by `init`:** When the Android runtime boots into `class main`, `init` parses `storage_guard.rc`, allocates `/dev/socket/storage_guard`, sets its DAC permissions (`0666`), assigns ownership to `system:system`, and spawns the daemon binary.

2. **Socket Inheritance:** The daemon executes `android_get_control_socket("storage_guard")` to adopt the file descriptor opened by `init`, avoiding race conditions or unnecessary file creation permissions. If executed outside `init`, it includes a fallback UNIX domain socket creation path.

3. **IPC Processing:** When `storage_guard_ctl` connects to `/dev/socket/storage_guard`, it sends a payload (e.g., `CHECK_DATA`, `CHECK_SYSTEM`, or `CHECK <custom_path>`).

4. **Filesystem Telemetry & Response:** The daemon invokes `StorageCollector::GenerateReportJson(...)`, which issues `statvfs()` on the requested mount point, calculates consumption percentage and thresholds, builds a JSON report, and streams it back over the socket.

## 3. Project File Breakdown & Key Code Analysis

### Directory Layout

When properly placed inside the AOSP tree, the project structure is:

```
system/extras/storage_guard/
├── Android.bp
├── storage_guard.rc
├── include/
│   └── storage_telemetry.h
├── lib/
│   └── storage_telemetry.cpp
└── src/
    ├── client_main.cpp
    └── daemon_main.cpp

```

### 3.1. `Android.bp` (Soong Blueprint Definition)

Defines three interconnected build targets:

- **`libstorage_telemetry` (`cc_library_shared`)**:
  - Compiles `lib/storage_telemetry.cpp`.

  - Exports headers in `include/` so dependent modules can `#include "storage_telemetry.h"`.

  - Links against standard AOSP runtime libraries: `libbase` (for string manipulation, logging) and `liblog` (for `logcat` support).

- **`storage_guard` (`cc_binary`)**:
  - Compiles `src/daemon_main.cpp`.

  - Declares `init_rc: ["storage_guard.rc"]`, ensuring the `.rc` script gets installed into `/system/etc/init/storage_guard.rc`.

  - Links against `libcutils` to gain access to `android_get_control_socket()`, `libstorage_telemetry` for metric calculations, and `libbase`/`liblog`.

- **`storage_guard_ctl` (`cc_binary`)**:
  - Compiles `src/client_main.cpp`.

  - Native shell executable intended for deployment to `/system/bin/`.

### 3.2. `storage_guard.rc` (Init Service Configuration)

```
service storage_guard /system/bin/storage_guard
    class main
    user system
    group system readproc
    capabilities SYS_ADMIN
    socket storage_guard stream 0666 system system

```

- **`service storage_guard /system/bin/storage_guard`**: Registers the binary located at `/system/bin/storage_guard`.

- **`class main`**: Tells `init` to start this daemon during the `main` boot class (alongside core system daemons).

- **`user system` & `group system readproc`**: Runs under UID `system`. Adding the `readproc` group grants access to read arbitrary process metadata under `/proc` if expanded.

- **`capabilities SYS_ADMIN`**: Grants POSIX Linux capability for deep administrative queries where default filesystem isolation might prevent reading superblock metadata.

- **`socket storage_guard stream 0666 system system`**: Instructs `init` to create a streaming socket at `/dev/socket/storage_guard` with world read/write access and assign ownership to `system:system`.

### 3.3. `storage_telemetry.h` & `storage_telemetry.cpp`

This library computes filesystem statistics using standard POSIX structures:

```
struct statvfs stat{};
if(statvfs(path.c_str(), &stat) != 0) { ... }

```

**Metric Computation:**

- `total_bytes = stat.f_blocks * stat.f_frsize` (Total filesystem size).

- `free_bytes = stat.f_bfree * stat.f_frsize` (Unallocated blocks).

- `available_bytes = stat.f_bavail * stat.f_frsize` (Blocks accessible to unprivileged processes).

- `used_percentage = ((total_bytes - free_bytes) / total_bytes) * 100.0`.

**Health State Thresholds:**

- $\ge 90.0\% \rightarrow \text{CRITICAL}$

- $\ge 75.0\% \rightarrow \text{WARNING}$

- $< 75.0\% \rightarrow \text{HEALTHY}$

The metrics are serialized directly to JSON via `GenerateReportJson()` without external third-party dependencies, keeping footprint minimal.

### 3.4. `daemon_main.cpp`

- **`ObtainServerSocket()`**:
  - Calls `android_get_control_socket("storage_guard")`. The Android init system populates environment variables referencing file descriptors created for defined service sockets.

  - If the descriptor is invalid (e.g., during independent test runs), it falls back to manually binding to `/dev/socket/storage_guard` via standard `bind()`.

- **`HandleClient(int client_fd)`**: Reads incoming requests, compares command tokens (`CHECK_DATA`, `CHECK_SYSTEM`, or `CHECK <path>`), invokes `StorageCollector`, and writes back the JSON string.

- **`main()`**: Uses `ab::InitLogging(argv, ab::LogdLogger())` to redirect all native `LOG(INFO)` and `PLOG(ERROR)` output to Android's `logd` daemon (`logcat`).

### 3.5. `client_main.cpp`

- CLI client connecting directly to `/dev/socket/storage_guard`.

- Defaults to `CHECK_DATA` if no arguments are passed.

- Writes the command to the socket, reads the response buffer into memory, and prints `[Daemon Response]: ...` directly to standard output.

## 4. AOSP Integration: File Edits & Placements

### 4.1. Source Code Placement

Place the entire `storage_guard` source directory inside `system/extras/`:

```
# From AOSP root directory:
mkdir -p system/extras/storage_guard
# Place Android.bp, storage_guard.rc, include/, lib/, and src/ here.

```

### 4.2. Editing Product Configurations (`handheld_system.mk`)

To guarantee that the daemon, library, and client are baked into the system image (`system.img`), you must add them to the product specification package list.

- **File Location:** `build/make/target/product/handheld_system.mk`

- **Why edit this file?** `handheld_system.mk` defines the software baseline installed onto the `/system` partition for all phone/tablet/emulator profiles inheriting from this product specification. Adding packages here ensures the Soong build system bundles them into the final flashable ext4/erofs `system.img`.

**Edit to apply:**
Open `build/make/target/product/handheld_system.mk`, navigate to `PRODUCT_PACKAGES += \`, and append:

```
PRODUCT_PACKAGES += \
    ... \
    storage_guard \
    storage_guard_ctl \
    libstorage_telemetry

```

### 4.3. Adding SELinux Policies

Android's Security-Enhanced Linux (SELinux) blocks unlabeled daemons from binding sockets or running in `init`. Add the following policies to your device or platform policy directories (for emulator/Goldfish, typically in `device/generic/goldfish/sepolicy/common/` or `system/sepolicy/private/`, Examples used are included in croot folder).

#### 1. File Contexts (`file_contexts`)

Tells SELinux what security label to apply to files on disk and sockets created at runtime.

```
/system/bin/storage_guard        u:object_r:storage_guard_exec:s0
/dev/socket/storage_guard        u:object_r:storage_guard_socket:s0

```

- **Why?** Identifies `/system/bin/storage_guard` as an executable entry point (`storage_guard_exec`) and marks `/dev/socket/storage_guard` as a dedicated socket label (`storage_guard_socket`).

#### 2. Domain Type Enforcement (`storage_guard.te`)

Defines process transitions and permissions:

```
type storage_guard, domain, coredomain;
type storage_guard_exec, exec_type, file_type, system_file_type;

# Automatically transitions init to domain 'storage_guard' when executing storage_guard_exec
init_daemon_domain(storage_guard)

# Set permissive during development and debugging
permissive storage_guard;

# Declare socket type
type storage_guard_socket, file_type, coredomain_socket;

# Allow init to create and manage the socket node
allow init storage_guard_socket:sock_file { create setattr unlink getattr };

# Allow daemon to use the socket
allow storage_guard storage_guard_socket:sock_file { getattr read write };

# Allow daemon to create and handle unix streaming connections
allow storage_guard self:unix_stream_socket { create_stream_socket_perms };

```

- **Why?** Without `init_daemon_domain`, `init` cannot execute the binary into its own context. Without `sock_file` and `unix_stream_socket` rules, SELinux denies file descriptor passing and client socket acceptance.

## 5. Build Commands

### Step 1: Environment Setup & Target Selection

Always initialize the AOSP build environment from the root directory:

```
source build/envsetup.sh
lunch sdk_phone64_x86_64-aosp_current-eng

```

### Step 2: Individual Component Build

To build only the modules without generating an entire system image:

```
m storage_guard storage_guard_ctl

```

### Step 3: Rebuilding SELinux Policy

When you modify or add `.te` or `file_contexts` rules:

```
m selinux_policy

```

### Step 4: Full System Image Generation

To compile all modules, package the filesystem, generate SELinux labels, and assemble `system.img`:

```
m storage_guard storage_guard_ctl && m selinux_policy && m systemimage

```

## 6. How to Run & Verify

### 6.1. Starting the Emulator

Launch your Goldfish emulator instance with the updated images:

```
emulator &

```

### 6.2. Executing the Client

Access the device shell or invoke `storage_guard_ctl` directly:

```
# Query the /data partition (default)
adb shell storage_guard_ctl

# Expected Output:
# [Daemon Response]:
# {"mount":"/data","total_mb":7628,"available_mb":5120,"used_pct":32.88,"status":"HEALTHY"}

# Query the /system partition
adb shell storage_guard_ctl CHECK_SYSTEM

# Query a custom mount point
adb shell storage_guard_ctl "CHECK /vendor"

```

### 6.3. Verifying Logs and Kernel Auditing

Inspect runtime operations and SELinux enforcement:

1. **Monitor Storage Guard Log Output:**
   Filter Android logcat directly for the daemon's log identifier:

   ```
   adb logcat -s storage_guard

   ```

   _Expected Logcat Output:_

   ```
   --------- beginning of main
   storage_guard: Starting storage_guard Daemon...
   storage_guard: Inherited socket descriptor: 3)
   storage_guard: storage_guard ready, listening connections
   storage_guard: Client command received: CHECK_DATA

   ```

2. **Check Kernel & SELinux Audits:**
   Verify if the kernel reported any denials or crashes:

   ```
   adb shell dmesg | grep -i "storage_guard"

   ```

   _If SELinux denials exist (in permissive mode), they will appear as `type=1400 audit(...) avc: denied ... for pid=... comm="storage_guard"` which can then be converted to rules using `audit2allow`._

## 7. Conclusion

The `storage_guard` subsystem provides a complete reference model for production-grade AOSP service integration. As an educational and practical exploration project, it demonstrates key platform concepts:

- Clean separation of concerns through native shared libraries.

- Robust IPC design utilizing `init`-allocated UNIX domain sockets.

- Adherence to modern Android architecture principles, including non-root security boundaries (`user system`), proper SELinux domain confinement, and declarative product integration through `handheld_system.mk`.
