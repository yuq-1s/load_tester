# http\_test

HTTP server load test tool

## Install

```
make
```

## Usage

```
./load_tester
  <hostname>              \
  <port>                  \
  <num_concurrency>       \
  <total_connection_num>  \
  <num_thread>
```

## TODO

1. `other response` contains those connections did not successfully transfer
`sizeof(GOOD\_HEADER)` bytes (which may be 200 or 500)

2. add progress bar

3. reuse sockaddr in `get\_socket`
