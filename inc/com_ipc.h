#ifndef COM_IPC_H
#define COM_IPC_H

// 这是一个 Umbrella Header (伞形头文件)
// 外部用户（如测试代码）只需要 include 这个文件，就能使用所有的 IPC 组件

#include "com_ipc/core/com_ipc_types.h"
#include "com_ipc/core/system_manager.h"
#include "com_ipc/api/publisher.h"
#include "com_ipc/api/subscriber.h"
#include "com_ipc/api/service_server.h"
#include "com_ipc/api/service_client.h"
#include "com_ipc/api/action_server.h"
#include "com_ipc/api/action_client.h"
#include "com_ipc/core/memory_pool.h"
#include "com_ipc/api/node.h"

#endif // COM_IPC_H