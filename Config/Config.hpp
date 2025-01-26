#pragma once

#include <bits/stdc++.h>
#include "Route.hpp"
class Config
{
public:
    std::set<int> ports;
    std::string host;
    std::set<std::string> allowed_methods;
    unsigned long long max_body_size;
    std::vector<std::string> server_names;
    std::map<int, std::string> error_pages;
    std::map<std::string, Route> routes; 

public:
    std::string config_content;
    Config();
    void    printConfig();
    void    parse_config(std::string confing_content);

    void    insertPort(std::string value);
    void    insertHost(std::string value);
    void    insertAllowedMethods(std::string value);
    void    insertMaxBodySize(std::string value);
    void    insertServerNames(std::string value);
    void    insertErrorPages(std::string value);
    void    insertRoute(std::string value);

};

std::string readConfigFile(std::string path);

