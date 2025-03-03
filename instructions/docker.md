## Docker

### view all containers
docker ps -a

### create a docker container, mount /LicheeRV-Nano-Build/ in host computer to container's workspace folder, and make it interactive and start bash
docker run -v /LicheeRV-Nano-Build/:/workspace -it licheervnano-build-ubuntu /bin/bash. 

### start and interactive with a stopped container
docker start -i CONTAINER_NAME_OR_ID

### stop a container
docker stop CONTAINER_NAME_OR_ID

### go inside a running container
docker exec -it <container_name_or_id> /bin/bash

## Bash

### copy file from remote server to local
scp <user>@<host>:<path> <destination>

### copy file from local to remote server
scp <path> <user>@<host>:<path>
