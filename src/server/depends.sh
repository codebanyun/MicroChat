#!/usr/bin/env bash

# ------------------------------
# 配置区：在这里更新服务名和对应的可执行文件路径
# 格式：[服务名]="可执行文件的完整相对路径"
# ------------------------------
declare -A MICROSERVICES=(
    ["gateway"]="./gateway/build/gateway_server"
    ["file"]="./file/build/file_server"
    ["friend"]="./friend/test/build/friend_server"
    ["message"]="./message/test/build/message_server"
    ["speech"]="./speech/build/speech_server"
    ["forward"]="./forward/test/build/forward_server"
    ["user"]="./user/test/build/user_server"
)

# ------------------------------
# 核心函数：提取可执行文件的依赖库到指定目录
# ------------------------------
get_depends() {
    local exe_path="$1"    # 第一个参数：可执行文件路径
    local target_dir="$2"  # 第二个参数：目标依赖目录

    # 1. 确保目标目录存在
    mkdir -p "$target_dir"

    # 2. 提取依赖库路径（过滤掉内核虚拟库，屏蔽 ldd 错误输出）
    local depends
    depends=$(ldd "$exe_path" 2>/dev/null | awk '{if (match($3,"/")){print $3}}')

    # 3. 复制依赖库（-L 跟随符号链接）
    if [ -n "$depends" ]; then
        cp -L $depends "$target_dir" 2>/dev/null || true
    fi
}

# ------------------------------
# 遍历关联数组，处理每个服务
# ------------------------------
for service in "${!MICROSERVICES[@]}"; do
    # 从关联数组中取出该服务对应的可执行文件路径
    build_exe="${MICROSERVICES[$service]}"
    
    echo "------------------------------"
    echo "正在处理服务: $service"
    echo "  可执行文件路径: $build_exe"
    
    # 定义该服务的根目录和依赖目录
    service_dir="./$service"
    depends_dir="$service_dir/depends"

    # 提取微服务自身的依赖
    if [ -f "$build_exe" ]; then
        echo "  正在提取 $service 服务的依赖..."
        get_depends "$build_exe" "$depends_dir"
        echo "  依赖提取完成，存放路径: $depends_dir"
    else
        echo "  [警告] 未找到可执行文件: $build_exe，跳过依赖提取"
    fi
done

echo "------------------------------"
echo "所有服务处理完成！"