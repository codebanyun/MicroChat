#pragma once
#include <elasticlient/client.h>
#include <cpr/cpr.h>
#include <json/json.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "spdlog.hpp"

namespace MicroChat {

    bool Serialize(const Json::Value &val, std::string &dst)
    {
        Json::StreamWriterBuilder writer;
        writer.settings_["emitUTF8"] = true;
        std::unique_ptr<Json::StreamWriter> json_writer(writer.newStreamWriter());
        std::stringstream os;
        json_writer->write(val, &os);
        dst = os.str();
        return true;
    }
    bool DeSerialize(const std::string &src, Json::Value &val)
    {
        Json::CharReaderBuilder reader;
        std::unique_ptr<Json::CharReader> json_reader(reader.newCharReader());
        std::string errs;
        bool ret = json_reader->parse(src.c_str(), src.c_str() + src.size(), &val, &errs);
        if (!ret)
        {
            LOG_ERROR("parse json failed, errs: {}", errs);
            return false;
        }
        return true;
    }
    class ESIndex
    {
        public:
            ESIndex(std::shared_ptr<elasticlient::Client> &client, const std::string &index_name, const std::string &doc_type = "_doc")
                :client_(client), index_name_(index_name), doc_type_(doc_type)
            {
                Json::Value ik;
                ik["tokenizer"] = "ik_max_word"; //中文分词器
                Json::Value analyzer;
                analyzer["ik"] = ik;
                Json::Value analysis;
                analysis["analyzer"] = analyzer;
                Json::Value settings;
                settings["analysis"] = analysis;
                index_["settings"] = settings;
            }
            /*
            *@brief: 添加索引属性
            *@param: key 属性名
            *@param: type 属性类型 text/keyword/integer/date等,text类型支持分词搜索，keyword类型不分词
            *@param: analyzer 分词器类型，默认使用ik_max_word分词器,只对text类型有效,standard表示不分词
            *@param: enable 是否启用索引，默认启用索引，设置为false表示不启用索引
            *@return: ESIndex引用，可用于链式调用
            */
            ESIndex& appendproperty(const std::string &key, 
                                    const std::string& type = "text",
                                    const std::string &analyzer = "ik_max_word",
                                    bool enable = true)
            {
                Json::Value field;
                field["type"] = type;
                field["analyzer"] = analyzer;
                if(!enable)
                {
                    field["index"] = "false";
                }
                properties_[key] = field;
                return *this;   
            }
            bool create(const std::string &index_id = "default_id")
            {
                Json::Value mappings;
                mappings["dynamic"] = "true";
                mappings["properties"] = properties_;
                index_["mappings"] = mappings;
                std::string body;
                if(!Serialize(index_, body))
                {
                    LOG_ERROR("serialize index failed");
                    return false;
                }
                LOG_DEBUG("{}", body);
                //发起创建索引请求
                try{
                    auto rsp = client_->index(index_name_, doc_type_, index_id, body);
                    if (rsp.status_code < 200 || rsp.status_code >= 300) {
                        LOG_ERROR("创建ES索引 {} 失败，响应状态码异常: {}", index_name_, rsp.status_code);
                        return false;
                    }
                }catch (const std::exception& e)
                {
                    LOG_ERROR("创建ES索引 {} 失败: {}", index_name_, e.what());
                    return false;
                }
                return true;
            }
        private:
            std::shared_ptr<elasticlient::Client> client_;
            std::string index_name_;
            std::string doc_type_;
            Json::Value index_;
            Json::Value properties_;
    };
    class ESInsert
    {   public:
            ESInsert(std::shared_ptr<elasticlient::Client> client , 
                     const std::string& name , 
                     const std::string& doc_type = "_doc")
                :client_(client), index_name_(name), doc_type_(doc_type)
            {}
            template<class T>
            ESInsert &append(const std::string &key, const T &val){
                item_[key] = val;
                return *this;
            }
            bool insert(const std::string& id)
            {
                std::string body;
                bool ret = Serialize(item_, body);
                if (ret == false) {
                    LOG_ERROR("索引序列化失败！");
                    return false;
                }
                LOG_DEBUG("{}", body);
                try {
                    auto rsp = client_->index(index_name_, doc_type_, id, body);
                    if (rsp.status_code < 200 || rsp.status_code >= 300) {
                        LOG_ERROR("向ES索引 {} 插入文档失败，响应状态码异常: {}", index_name_, rsp.status_code);
                        return false;
                    }
                } catch (const std::exception &e) {
                    LOG_ERROR("向ES索引 {} 插入数据失败: {}", index_name_, e.what());
                    return false;
                }
                return true;
            }
        private:
            std::shared_ptr<elasticlient::Client> client_;
            std::string index_name_;
            std::string doc_type_;
            Json::Value item_;
    };
    class ESRemove{
        public:
            ESRemove(std::shared_ptr<elasticlient::Client> client , 
                     const std::string& name , 
                     const std::string& doc_type = "_doc")
                :client_(client), index_name_(name), doc_type_(doc_type)
            {}
            bool remove(const std::string& id)
            {
                try {
                    auto rsp = client_->remove(index_name_, doc_type_, id);
                    if (rsp.status_code < 200 || rsp.status_code >= 300) {
                        LOG_ERROR("从ES索引 {} 删除文档失败，响应状态码异常: {}", index_name_, rsp.status_code);
                        return false;
                    }
                } catch (const std::exception &e) {
                    LOG_ERROR("从ES索引 {} 删除文档失败: {}", index_name_, e.what());
                    return false;
                }
                return true;
            }
        private:
            std::shared_ptr<elasticlient::Client> client_;
            std::string index_name_;
            std::string doc_type_;
    };
    class ESSearch{
        public:
            ESSearch(std::shared_ptr<elasticlient::Client> client , 
                     const std::string& name , 
                     const std::string& doc_type = "_doc")
                :client_(client), index_name_(name), doc_type_(doc_type)
            {}
            ESSearch& set_query(const Json::Value& query)
            {
                search_["query"] = query;
                return *this;
            }
            bool search(Json::Value& result)
            {
                std::string body;
                bool ret = Serialize(search_, body);
                if (ret == false) {
                    LOG_ERROR("搜索序列化失败！");
                    return false;
                }
                LOG_DEBUG("{}", body);
                try {
                    auto rsp = client_->search(index_name_, doc_type_, body);
                    if (rsp.status_code < 200 || rsp.status_code >= 300) {
                        LOG_ERROR("ES索引 {} 搜索失败，响应状态码异常: {}", index_name_, rsp.status_code);
                        return false;
                    }
                    ret = DeSerialize(rsp.text, result);
                    if (ret == false) {
                        LOG_ERROR("搜索结果反序列化失败！");
                        return false;
                    }
                } catch (const std::exception &e) {
                    LOG_ERROR("ES索引 {} 搜索失败: {}", index_name_, e.what());
                    return false;
                }
                return true;
            }
            ESSearch& append_should_match(const std::string &key, const std::string &val) {
                Json::Value field;
                field[key] = val;
                Json::Value match;
                match["match"] = field;
                should_.append(match);
                return *this;
            }
            ESSearch& append_must_not_terms(const std::string &key, const std::vector<std::string> &vals) {
                Json::Value fields;
                for (const auto& val : vals){
                    fields[key].append(val);
                }
                Json::Value terms;
                terms["terms"] = fields;
                must_not_.append(terms);
                return *this;
            }
            //必须匹配的字段
            ESSearch& append_must_term(const std::string &key, const std::string &val) {
                Json::Value field;
                field[key] = val;
                Json::Value term;
                term["term"] = field;
                must_.append(term);
                return *this;
            }
            ESSearch& append_must_match(const std::string &key, const std::string &val){
                Json::Value field;
                field[key] = val;
                Json::Value match;
                match["match"] = field;
                must_.append(match);
                return *this;
            }
            Json::Value search(){
                Json::Value cond;
                if (!must_not_.empty()) cond["must_not"] = must_not_;
                if (!should_.empty()) cond["should"] = should_;
                if (!must_.empty()) cond["must"] = must_;
                Json::Value query;
                query["bool"] = cond;
                Json::Value root;
                root["query"] = query;

                std::string body;
                bool ret = Serialize(root, body);
                if (ret == false) {
                    LOG_ERROR("索引序列化失败！");
                    return Json::Value();
                }
                LOG_DEBUG("{}", body);
                //2. 发起搜索请求
                cpr::Response rsp;
                try {
                    rsp = client_->search(index_name_, doc_type_, body);
                    if (rsp.status_code < 200 || rsp.status_code >= 300) {
                        LOG_ERROR("检索数据 {} 失败，响应状态码异常: {}", body, rsp.status_code);
                        return Json::Value();
                    }
                } catch(std::exception &e) {
                    LOG_ERROR("检索数据 {} 失败: {}", body, e.what());
                    return Json::Value();
                }
                //3. 需要对响应正文进行反序列化
                LOG_DEBUG("检索响应正文: [{}]", rsp.text);
                Json::Value json_res;
                ret = DeSerialize(rsp.text, json_res);
                if (ret == false) {
                    LOG_ERROR("检索数据 {} 结果反序列化失败", rsp.text);
                    return Json::Value();
                }
                return json_res["hits"]["hits"];
            }
        private:
            std::shared_ptr<elasticlient::Client> client_;
            std::string index_name_;
            std::string doc_type_;
            Json::Value must_not_;
            Json::Value should_;
            Json::Value must_;
            Json::Value search_;
    };
} //namespace MicroChat