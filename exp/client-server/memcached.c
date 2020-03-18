#include <libmemcached/memcached.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  //memcached_servers_parse (char *server_strings);
  memcached_server_st *servers = NULL;
  memcached_st *memc;
  memcached_return rc;
  char *key = "keystring";
  char *value = "keyvalue";

  char *retrieved_value;
  size_t value_length;
  uint32_t flags;

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, "10.0.0.1", 11211, &rc);
  rc = memcached_server_push(memc, servers);

  if (rc == MEMCACHED_SUCCESS)
    fprintf(stderr, "Added server successfully\n");
  else
    fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));

  rc = memcached_set(memc, key, strlen(key), value, strlen(value), (time_t)0, (uint32_t)0);

  if (rc == MEMCACHED_SUCCESS)
    fprintf(stderr, "Key stored successfully\n");
  else
    fprintf(stderr, "Couldn't store key: %s\n", memcached_strerror(memc, rc));

  retrieved_value = memcached_get(memc, key, strlen(key), &value_length, &flags, &rc);
  printf("Yay!\n");

  if (rc == MEMCACHED_SUCCESS) {
    fprintf(stderr, "Key retrieved successfully\n");
    printf("The key '%s' returned value '%s'.\n", key, retrieved_value);
    free(retrieved_value);
  }
  else
    fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));

  return 0;
}
