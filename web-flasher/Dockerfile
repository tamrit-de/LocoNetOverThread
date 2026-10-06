FROM node:22-alpine AS dependencies

WORKDIR /web
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

FROM nginx:stable-alpine

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY index.html app.js style.css /usr/share/nginx/html/
COPY --from=dependencies /web/node_modules/esptool-js/bundle.js /usr/share/nginx/html/vendor/esptool.js
COPY --from=dependencies /web/node_modules/esptool-js/LICENSE /usr/share/nginx/html/vendor/esptool-js-LICENSE.txt
COPY --from=dependencies /web/node_modules/pako/LICENSE /usr/share/nginx/html/vendor/pako-LICENSE.txt
COPY --from=dependencies /web/node_modules/atob-lite/LICENSE.md /usr/share/nginx/html/vendor/atob-lite-LICENSE.txt
