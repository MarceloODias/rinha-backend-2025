# rtb-index-exchange
Repository for the RealTimeBidding for Index Exchange

## Build

### To build the docker image
```bash
docker build -t engineeringinpowered/dev_rtb-index-exchange:v1.1 .
```

### To run docker image
```bash
docker run -d --network="host" -t --rm --name rtb-index-exchange -v /var/log/rtb:/var/log/rtb -e PRINT_LOGS_ENABLED="1" -e PRINT_PATH="/var/log/rtb/" -e CONCURRENCY="1" -e RANDOM_DATA_ENABLED="1" engineeringinpowered/dev_rtb-index-exchange:v1.5
docker run -d --network="host" -t --rm --name rtb-index-exchange -v /var/log/rtb:/var/log/rtb -e RANDOM_DATA_ENABLED="1" engineeringinpowered/dev_rtb-index-exchange:v1.7
docker run -d --network="host" -t --rm --name rtb-index-exchange engineeringinpowered/dev_rtb-index-exchange:v1.5
```

### To run the performance test
```bash
docker run --network="host" -t --rm --name send-rtd-requests -e COMMAND_LINE_FLAGS="-e http://localhost:8080/enrichment -q 10000" indexexchangehub/ext-data-provider-test-tools:2024.11.20
docker run --network="host" -t --rm --name send-rtd-requests -e COMMAND_LINE_FLAGS="-e http://172.16.100.32:8080/enrichment -q 10000" indexexchangehub/ext-data-provider-test-tools:2024.11.20
```

### To use other docker env (should be used with caution as IX doesn't support optimization flags)
```bash
export DOCKER_HOST=docker-runner.inpwrd.net:2375
```


## To simulate the scale

### Access the machine (rtb-stress-testing-3)
```
ssh -i "dev.pem" ec2-user@172.16.100.32
```

### Download of the json
```
aws s3 cp s3://inpowered-mlflow-prod/80/cf6f87d241974cedaea26f8e8fee4cec/artifacts/best_model_info.json hyundai_click.json
```

### Download of the cbm
```
aws s3 cp s3://ml-training-prod/dv360/models/hyundai_click_2025_03_24_v21.cbm .
```

### Run the docker
```
docker run --network="host" -t --rm --name rtb-index-exchange -v /home/ec2-user/models:/home/ec2-user/models -e MODELS_PATH="/home/ec2-user/models/" engineeringinpowered/dev_rtb-index-exchange:v1.6
```

### Run the tester docker, let is running for some time
```
docker run --network="host" -t --rm --name send-rtd-requests -e COMMAND_LINE_FLAGS="-e http://localhost:8080/enrichment -q 1000 -c 10" indexexchangehub/ext-data-provider-test-tools:2024.11.20
```

### Get the results
```
curl --location 'http://172.16.100.32:8080/results'
```