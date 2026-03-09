#ifndef COM_IPC_H
#define COM_IPC_H

// 这是一个 Umbrella Header (伞形头文件)
// 外部用户（如测试代码）只需要 include 这个文件，就能使用所有的 IPC 组件

#include "com_ipc_types.h"
#include "system_manager.h"
#include "publisher.h"
#include "subscriber.h"
#include "service_server.h"
#include "service_client.h"
#include "action_server.h"
#include "action_client.h"
#include "memory_pool.h"
#include "node.h"

#endif // COM_IPC_H