#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include <algorithm>
#include <stdexcept>

HttpRequest::HttpRequest() : _pos(0), _bufferLen(0), state(REQUESTLINE) {}

HttpRequest::~HttpRequest() {}

HttpRequest::HttpRequest(const Config& _configs)
    :   _pos(0), _bufferLen(0),
        configs(_configs),
        autoIndex(false),statusCode(200),
        fileCreated(0),_currentChunkSize(-1),_totalBodysize(0), _currentChunkBytesRead(0),  isChunked(0), state(REQUESTLINE) {}


size_t HttpRequest::parse(const uint8_t *buffer, size_t bufferLen)
{
    this->_bufferLen = bufferLen;
    this->_buffer = buffer;
    size_t bytesReceived = 0;
    try {
        if (state == REQUESTLINE)
            bytesReceived += parseRequestLine();
        if (state == HEADERS)
            bytesReceived += parseHeaders();
        if (state == BODY) {
            if (headers.count("content-length") || isChunked) {
                bytesReceived += parseBody();

            } else {
                state = COMPLETE;
            }
        }
        if (state == COMPLETE)
            return bytesReceived;
    } catch (int code) {
        setStatusCode(code);
        request.clear();
        state = COMPLETE;
        return bytesReceived;
    }
    catch (const HttpIncompleteRequest &e)
    {
        return bytesReceived;
    }
    return bytesReceived;
}

size_t HttpRequest::parseRequestLine()
{
    std::string line = getLineAsString(readLine());
    if (line.empty())
        throw(HttpIncompleteRequest());
    size_t firstSpace = line.find(' ');
    size_t secondSpace = line.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos)
        throw 400;
    if (line.find(' ', secondSpace + 1) != std::string::npos)
        throw 400;
    method = line.substr(0, firstSpace);
    uri = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    version = line.substr(secondSpace + 1);
    validateMethod();
    validateURI();
    RouteURI();
    validateVersion();
    state = HEADERS;
    return (line.size() + 2);
}

std::string HttpRequest::getUploadDir()
{
    Route &routeConf = getConfig().getRoutes()[RequestrouteKey];
    if (!routeConf.upload_dir.empty())
    {
        return std::string(routeConf.upload_dir);
    }
    return "www/html/uploads/";
}

void HttpRequest::reset()
{
    _buffer = 0;
    _bufferLen = 0;
    bodyStart = 0;
    defaultIndex.clear();
    autoIndex = false;
    statusCode = 200;
    fileCreated = false;
    outfilename.clear();
    _currentChunkSize = -1;
    _totalBodysize = 0;
    _currentChunkBytesRead = 0;
    method.clear();
    uriPath.clear();
    version.clear();
    body.clear();
    uri.clear();
    request.clear();
    originalUri.clear();
    uriQueryParams.clear();
    headers.clear();
    RequestrouteKey.clear();
    isChunked = false;
    keepAlive = true;
    _pos = 0;
    autoIndex = false;
    state = REQUESTLINE;
}

void HttpRequest::RouteURI()
{
    Config &conf = configs;
    std::string routeKey;
    std::map<std::string, Route>::iterator routeIt;
    std::map<std::string, Route> routesMap = conf.getRoutes();

    for (routeIt = routesMap.begin(); routeIt != routesMap.end(); routeIt++)
    {
        if (uriPath.find(routeIt->first) == 0)
        {
            if (routeIt->first.size() > routeKey.size())
            {
                routeKey = routeIt->first;
            }
        }
    }
    if (!routeKey.empty())
    {
        routeConf = routesMap[routeKey];
        RequestrouteKey = routeKey; 
        defaultIndex = routeConf.getDefaultFile();
        autoIndex = routeConf.getAutoIndexState();
        std::string ROUTE = (routeConf.getRoot());
        if (ROUTE.find("..") != std::string::npos) throw 403;
        if (routeKey == "/")
        {
            if (size_t pos = uriPath.find('/') != std::string::npos)
                uriPath.replace(pos - 1, 1, ROUTE + "/");
        }
        else
        {
            if (size_t pos = uriPath.find(routeKey) != std::string::npos)
            {
                uriPath.replace(pos - 1, routeKey.size(), ROUTE);
            }
        }
    }
    setStatusCode(checkFilePerms(uriPath));
}

std::string HttpRequest::getRequestrouteKey()
{
    return RequestrouteKey;
}

void HttpRequest::validateMethod()
{
    if (method != "GET" && method != "DELETE" && method != "POST")
        throw 501;
}

void HttpRequest::validateURI()
{
    if (uri.empty())
    {
        throw 400;
    }
    if (uri.size() > 2048)
    {
        throw 414;
    }

    size_t queryPos = uri.find('?');
    std::string query;
    if (queryPos != std::string::npos)
    {
        uriPath = uri.substr(0, queryPos);
        query = uri.substr(queryPos + 1);
    }
    else
    {
        uriPath = uri;
        query.clear();
    }
    if (!isAbsoluteURI())
    {
        if (uriPath[0] != '/')
            throw 400;
    }
    else
    {
        size_t schemeEnd = uriPath.find('/');
        if (schemeEnd != std::string::npos)
            uriPath = uriPath.substr(schemeEnd + 2);
        else
            uriPath = "/";
    }
    originalUri = uriPath;
    uriPath = decodeAndNormalize();
    if (!query.empty())
        uriQueryParams = decodeAndParseQuery(query);
}

std::map<std::string, std::string> HttpRequest::decodeAndParseQuery(std::string &query)
{
    std::istringstream iss(query);
    std::map<std::string, std::string> queryParams;
    std::string queryParam, key, value;
    while (std::getline(iss, queryParam, '&'))
    {
        size_t pos = queryParam.find('=');
        key = queryParam.substr(0, pos);
        value = (pos != std::string::npos ? queryParam.substr(pos + 1) : "");
        if (value.find('#') != std::string::npos)
            value.erase(value.find('#'));
        queryParams[decode(key)] = decode(value); 
    }
    return (queryParams);
}

std::string HttpRequest::decode(std::string &encoded)
{
    std::string decoded;

    for (size_t i = 0; i < encoded.size(); i++)
    {
        if (encoded[i] == '%')
        {
            if (i + 2 >= encoded.size())
                throw 400;
            if (!isHexDigit(encoded[i + 1]) || !isHexDigit(encoded[i + 1]))
                throw 400;
            decoded += hexToValue(encoded[i + 1]) * 16 + hexToValue(encoded[i + 2]);
            i += 2;
        }
        else if (!isURIchar(encoded[i]))
            throw 400;
        else
            decoded += encoded[i];
    }
    return (decoded);
}

std::string HttpRequest::normalize(std::string &decoded)
{
    std::istringstream iss(decoded);
    std::string segment;
    std::vector<std::string> normalizedSegments;
    std::string normalized;
    while (std::getline(iss, segment, '/'))
    {
        if (segment == ".")
            continue;
        else if (segment == "..")
        {
            if (!normalizedSegments.empty())
                normalizedSegments.pop_back();
        }
        else
            normalizedSegments.push_back(segment);
    }
    for (size_t i = 0; i < normalizedSegments.size(); i++)
    {
        if (normalizedSegments[i].empty())
            continue;
        normalized += "/" + normalizedSegments[i];
    }
    if (normalized.empty())
        return ("/");
    return (normalized);
}

std::string HttpRequest::decodeAndNormalize()
{
    std::istringstream iss(uriPath);
    std::string segment;
    std::string decodedAndNormalized;
    bool hasTrailingSlash = uriPath[uriPath.size() - 1] == '/';

    while (std::getline(iss, segment, '/'))
        decodedAndNormalized += "/" + decode(segment);
    std::string normalized = normalize(decodedAndNormalized);
    if (hasTrailingSlash)
        normalized += "/";
    return (normalized);
}

bool HttpRequest::isAbsoluteURI()
{
    return (uri.find("http://") == 0 || uri.find("https://") == 0);
}

bool HttpRequest::isURIchar(char c)
{
    const std::string allowed = "!$&'()*+,-./:;=@_~";
    return (std::isalnum(c) || allowed.find(c) != std::string::npos);
}

void HttpRequest::validateVersion()
{
    if (version.empty() || version != "HTTP/1.1")
        throw 505;
}

size_t HttpRequest::parseHeaders()
{
    size_t startPos = _pos;
    std::string line = getLineAsString(readLine());
    while (!line.empty())
    {
        std::pair<std::string, std::string> keyValue = splitKeyValue(line, ':');
        std::string key = toLowerCase(keyValue.first);
        std::string value = strTrim(keyValue.second);
        if (key.empty() || key.find_first_of(" \t") != std::string::npos || key.find_last_of(" \t") != std::string::npos)
            throw 400;
        if (headers.find(key) != headers.end())
            headers[key] += "," + value;
        else
            headers[key] = value;
        line = getLineAsString(readLine());
    }

    bool transferEncodingFound = headers.count("transfer-encoding");
    if (transferEncodingFound && toLowerCase(headers["transfer-encoding"]) != "chunked")
    {
        throw 501;
    }
    isChunked = transferEncodingFound && toLowerCase(headers["transfer-encoding"]) == "chunked";
    if (isChunked && headers.count("content-length")) { throw 400; }
    if (method == "POST" && !isChunked && !headers.count("content-length")) { throw 400; }
    if (headers.count("host") == 0)
    {
        throw 400;
    }

    state = BODY;
    bodyStart = _pos;
    return (_pos - startPos);
}

std::string getFancyFilename() {
    static long counter = 0;
    std::time_t now = std::time(0);
    std::tm *gmt = std::gmtime(&now);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%a-%d-%b-%Y-%H:%M:%S-GMT", gmt);
    return std::string(buffer) + "-" + toString(counter++);
}

bool HttpRequest::isCreatFile() const
{
    size_t extPos = uriPath.rfind('.');
    if (extPos == std::string::npos || extPos == uriPath.length() - 1) {
        return false;
    }
    std::string ext = uriPath.substr(extPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".php" || ext == ".py" || ext == ".sh");
}

size_t HttpRequest::parseBody()
{
    size_t startPos = _pos;

    std::set<std::string> &methods = routeConf.getAllowedMethods();
    if (methods.count(method) == 0) {
       throw 405;
    }
    if (isChunked)
    {
        return parseChunkedBody();
    }

    unsigned long long contentLength;
    try { contentLength = atoull(headers["content-length"]); }
    catch (...) { throw 400; }
    if (contentLength > getConfig().max_body_size) { throw 413; }

    if (_bufferLen - _pos < static_cast<size_t>(contentLength))
    {
        throw(HttpIncompleteRequest());
    }
    body.insert(body.end(), _buffer + _pos, _buffer + _pos + contentLength);
    
    if (!isCreatFile()) {
        outfilename = getUploadDir() + "FILE_" + getFancyFilename();
        std::ofstream ofile(outfilename.c_str(), std::ios::binary);
        if (!ofile.is_open()) { throw 500; }
        ofile.write((const char *)(_buffer + _pos), contentLength);
        ofile.close();
    }
    _pos += contentLength;
    state = COMPLETE;
    return (_pos - startPos);
}

size_t HttpRequest::parseChunkedBody()
{
    size_t startPos = _pos;

    if (!fileCreated)
    {
        outfilename = getUploadDir() + "XFILE_" + getFancyFilename();
        fileCreated = true;
    }
    std::ofstream ofile(outfilename.c_str(), std::ios::app | std::ios::binary);
    if (!ofile.is_open())
    {
        throw 500;
    }

    try
    {
        while (_pos < _bufferLen)
        {
            if (_currentChunkSize == -1)
            {
                std::string sizeStr = getLineAsString(readLine());
                _currentChunkSize = _16_to_10(sizeStr);
                if (_currentChunkSize < 0)
                {
                    throw 400;
                }
                else if (_currentChunkSize == 0)
                {
                    state = COMPLETE;
                    readLine();
                    break;
                }
                _totalBodysize += _currentChunkSize;
                if (_totalBodysize > getConfig().max_body_size)
                {
                    std::remove(outfilename.c_str());
                    throw 413;
                }
            }
            size_t remainingInChunk = _currentChunkSize - _currentChunkBytesRead;
            size_t availableData = _bufferLen - _pos;
            size_t bytesToRead = std::min(remainingInChunk, availableData);

            if (bytesToRead == 0)
            {
                throw HttpIncompleteRequest();
            }
            body.insert(body.end(), _buffer + _pos, _buffer + _pos + bytesToRead);
            ofile.write((const char *)(_buffer + _pos), bytesToRead);
            if (ofile.bad())
            {
                std::remove(outfilename.c_str());
                throw 500;
            }
            _pos += bytesToRead;
            _currentChunkBytesRead += bytesToRead;

            if (_currentChunkBytesRead == (size_t)_currentChunkSize) {
                readLine();
                _currentChunkSize = -1;
                _currentChunkBytesRead = 0;
            }
        }
    }
    catch (const HttpIncompleteRequest &)
    {
        ofile.close();
        return (_pos - startPos);
    }
    ofile.close();
    return (_pos - startPos);
}

std::vector<uint8_t> HttpRequest::readLine()
{
    std::vector<uint8_t> lineBuffer;
    size_t start = _pos;

    while (_pos < _bufferLen)
    {
        if (_buffer[_pos] == '\r' && _pos + 1 < _bufferLen && _buffer[_pos + 1] == '\n')
        {
            lineBuffer.insert(lineBuffer.end(), _buffer + start, _buffer + _pos);
            _pos += 2;
            return (lineBuffer);
        }
        _pos++;
    }
    _pos = start;
    throw(HttpIncompleteRequest());
}

std::pair<std::string, std::string> HttpRequest::splitKeyValue(const std::string &toSplit, char delim)
{
    size_t keyValue = toSplit.find(delim);
    std::string key = toSplit.substr(0, keyValue);
    std::string value = (keyValue != std::string::npos ? toSplit.substr(keyValue + 1) : "");
    return (std::make_pair(key, value));
}


bool HttpRequest::isImplemented(std::string str) {
    if (str.find("multipart/form-data") != std::string::npos) return false;
    if (str.find("x-www-form-urlencoded") != std::string::npos) return false;
    (void)str;
    return true;
}


std::string HttpRequest::getLineAsString(const std::vector<uint8_t>& line) {
    return std::string(reinterpret_cast<const char*>(line.data()), line.size());
}

std::string     HttpRequest::getHeaderValue(std::string key) {
    return headers.find(key) != headers.end() ? headers[key] : "nah"; 
}