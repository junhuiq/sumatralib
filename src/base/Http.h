/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct HttpRsp {
    Str url;
    StrBuilder data;
    DWORD error = (DWORD)-1;
    DWORD httpStatusCode = (DWORD)-1;

    HttpRsp() = default;
    ~HttpRsp();
};

struct HttpProgress {
    i64 nDownloaded;
};

bool IsHttpRspOk(const HttpRsp*);

bool HttpPost(Str server, int port, Str url, StrBuilder* headers, StrBuilder* data);
bool HttpPostWithResp(Str server, int port, Str url, StrBuilder* headers, StrBuilder* data, StrBuilder* rspOut, unsigned int timeoutMs = 15000);
bool HttpGet(Str url, HttpRsp* rspOut);
bool HttpGetToFile(Str url, Str destFilePath, const Func1<HttpProgress*>& cbProgress);
