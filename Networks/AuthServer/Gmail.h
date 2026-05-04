#pragma once
#include <curl/curl.h>
#include <string>


struct upload_status {
    int lines_read;
};

static const char* payload_text[] = {
    "To: recipient@example.com\r\n",
    "From: sender@gmail.com\r\n",
    "Subject: Test Mail\r\n",
    "\r\n",
    "Hello, this is a test email sent using libcurl.\r\n",
    NULL
};

size_t payload_source(void* ptr, size_t size, size_t nmemb, void* userp) {
    upload_status* upload = (upload_status*)userp;
    const char* data;

    if ((size == 0) || (nmemb == 0) || ((size * nmemb) < 1))
        return 0;

    data = payload_text[upload->lines_read];

    if (data) {
        size_t len = strlen(data);
        memcpy(ptr, data, len);
        upload->lines_read++;
        return len;
    }

    return 0;
}

void TestSend() {
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();

    if (curl) {
        struct curl_slist* recipients = NULL;
        upload_status upload_ctx = { 0 };

        curl_easy_setopt(curl, CURLOPT_USERNAME, "Blockia.server@gmail.com");
        curl_easy_setopt(curl, CURLOPT_PASSWORD, "PASSWORD");

        curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");

        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "<Blockia.server@gmail.com>");

        recipients = curl_slist_append(recipients, "<MAIL>");
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        res = curl_easy_perform(curl);

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }
}