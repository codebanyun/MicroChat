# Elasticsearch + IK 自定义镜像

## 1) 构建镜像

```bash
docker build \
  -t microchat/elasticsearch-ik:7.17.21 \
  --build-arg ES_VERSION=7.17.21 \
  --build-arg IK_VERSION=7.17.21 \
  -f elasticsearch-ik/Dockerfile \
  .
```

## 2) 本地验证插件

```bash
docker run --rm microchat/elasticsearch-ik:7.17.21 \
  /usr/share/elasticsearch/bin/elasticsearch-plugin list
```

输出中看到 `analysis-ik` 即表示安装成功。

## 3) Compose 中使用

将 `docker-compose.yml` 里的 ES 镜像改为：

```yaml
elasticsearch:
  image: microchat/elasticsearch-ik:7.17.21
```

## 4) 打包留档（便于后续复用）

```bash
docker save -o microchat-elasticsearch-ik-7.17.21.tar microchat/elasticsearch-ik:7.17.21
```

在其他机器导入：

```bash
docker load -i microchat-elasticsearch-ik-7.17.21.tar
```

## 5) 若下载插件失败

可替换构建参数 `IK_PLUGIN_URL`：

```bash
docker build \
  -t microchat/elasticsearch-ik:7.17.21 \
  --build-arg ES_VERSION=7.17.21 \
  --build-arg IK_VERSION=7.17.21 \
  --build-arg IK_PLUGIN_URL=<可访问的IK插件zip地址> \
  -f elasticsearch-ik/Dockerfile \
  .
```
