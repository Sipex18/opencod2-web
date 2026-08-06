#!/bin/bash
sed -i '0,/gzip off;/{s/gzip off;/gzip_static on;\n        gzip off;/}' /etc/nginx/sites-available/cod2
grep gzip /etc/nginx/sites-available/cod2
nginx -t && systemctl reload nginx && echo "OK: nginx reloaded"
