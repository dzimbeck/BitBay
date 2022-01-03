FROM alpine:3.8

ENV RPC_USER bitbayrpc
ENV RPC_PASS pegforever

ADD bitbayd /usr/bin/bitbayd
ADD baystart.sh /baystart.sh
ENTRYPOINT ["/baystart.sh"]
