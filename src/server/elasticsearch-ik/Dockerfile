ARG ES_VERSION=7.17.21
FROM elasticsearch:${ES_VERSION}

ARG IK_VERSION=7.17.21
ARG IK_PLUGIN_URL=https://release.infinilabs.com/analysis-ik/stable/elasticsearch-analysis-ik-${IK_VERSION}.zip

RUN /usr/share/elasticsearch/bin/elasticsearch-plugin install --batch ${IK_PLUGIN_URL}
