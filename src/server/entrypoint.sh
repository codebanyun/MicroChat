#!/usr/bin/env bash

# ------------------------------
# 配置区：可调整的参数
# ------------------------------
WAIT_TIMEOUT=60  # 单个端口最大等待时间（秒）
WAIT_INTERVAL=1  # 探测间隔（秒）

# ------------------------------
# 工具函数：打印带时间戳的日志
# ------------------------------
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

wait_for() {
    local host="$1"
    local port="$2"
    local timeout="${3:-$WAIT_TIMEOUT}" # 如果没传超时时间，用默认值
    local start_time=$(date +%s)
    
    # 检查参数是否为空
    if [ -z "$host" ] || [ -z "$port" ]; then
        log "[错误] wait_for 缺少 host 或 port 参数"
        return 1
    fi

    log "[信息] 开始探测 $host:$port，超时时间 ${timeout}秒..."
    
    while true; do
        # 用 nc 探测，增加 -w 参数指定 nc 自身的超时时间（避免 nc 卡死）
        if nc -z -w 2 "$host" "$port" >/dev/null 2>&1; then
            log "[成功] $host:$port 连接正常！"
            return 0
        fi

        # 检查是否超时
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))
        if [ "$elapsed" -ge "$timeout" ]; then
            log "[错误] $host:$port 探测超时（已等待 ${elapsed}秒），脚本退出！"
            return 1
        fi

        log "[等待] $host:$port 连接失败，已等待 ${elapsed}秒，${WAIT_INTERVAL}秒后重试..."
        sleep "$WAIT_INTERVAL"
    done
}

# ------------------------------
# 参数解析 + 强制校验
# ------------------------------
usage() {
    echo "用法: $0 (-h <host> -p <ports> | -t <targets>) -c <command>"
    echo "  -h: 依赖服务的 IP/主机名（旧模式）"
    echo "  -p: 逗号分隔的端口列表（旧模式，如 3306,2379,6379）"
    echo "  -t: 逗号分隔的 host:port 列表（推荐，如 etcd:2379,mysql:3306）"
    echo "  -c: 依赖就绪后执行的启动命令（需加引号）"
    echo "示例: $0 -t etcd:2379,mysql:3306,redis:6379 -c './file_server -flagfile=xx.conf'"
    exit 1
}

# 初始化变量
ip=""
ports=""
targets=""
command=""

# 解析参数
while getopts "h:p:t:c:" arg; do
    case "$arg" in
        h) ip="$OPTARG" ;;
        p) ports="$OPTARG" ;;
        t) targets="$OPTARG" ;;
        c) command="$OPTARG" ;;
        *) usage ;; # 遇到未知参数，打印用法并退出
    esac
done

# 强制校验参数是否为空
if [ -z "$command" ]; then
    log "[错误] 缺少必要参数！"
    usage
fi

if [ -z "$targets" ] && { [ -z "$ip" ] || [ -z "$ports" ]; }; then
    log "[错误] 旧模式需要同时提供 -h 和 -p，或直接使用 -t"
    usage
fi

# ------------------------------
# 检查 nc 是否安装
# ------------------------------
if ! command -v nc &> /dev/null; then
    log "[错误] 未找到 nc（netcat）工具，请先安装！"
    exit 1
fi

# ------------------------------
# 可选的启动前延迟
# ------------------------------
STARTUP_DELAY="${STARTUP_DELAY:-0}"
if [[ "$STARTUP_DELAY" =~ ^[0-9]+$ ]] && [ "$STARTUP_DELAY" -gt 0 ]; then
    log "[信息] 启动前延迟 ${STARTUP_DELAY} 秒，等待中间件完全稳定..."
    sleep "$STARTUP_DELAY"
fi

# ------------------------------
# 遍历端口探测
# ------------------------------
log "[信息] 开始端口探测阶段..."
if [ -n "$targets" ]; then
    for target in ${targets//,/ }; do
        host="${target%:*}"
        port="${target##*:}"
        if [ -z "$host" ] || [ -z "$port" ] || [ "$host" = "$port" ]; then
            log "[错误] 非法目标: $target，正确格式应为 host:port"
            exit 1
        fi
        wait_for "$host" "$port" || exit 1
    done
else
    # 把逗号替换成空格，注意加双引号避免空格问题
    for port in ${ports//,/ }; do
        # 调用 wait_for，如果失败直接退出脚本
        wait_for "$ip" "$port" || exit 1
    done
fi

# ------------------------------
# 执行启动命令
# ------------------------------
log "[信息] 所有依赖就绪，开始执行启动命令: $command"
exec $command