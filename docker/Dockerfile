FROM docker-picodata.binary.picodata.io/oracle-connector-builder:21.7.0

WORKDIR /app

COPY build/oracle-connector.el8.tgz /tmp/

RUN tar -xvf /tmp/oracle-connector.el8.tgz -C / && rm /tmp/oracle-connector.el8.tgz
