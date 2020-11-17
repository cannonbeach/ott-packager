#!/bin/bash
# make sure we don't delete the init.mp4
find /var/www/html/hls -name "segment*.mp4" -mmin +30 -delete
find /var/www/html/hls -name "video*.ts" -mmin +30 -delete
find /var/www/html/hls -name "audio*.ts" -mmin +30 -delete
