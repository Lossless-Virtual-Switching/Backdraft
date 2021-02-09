import sys
import subprocess
import yaml


def get_mac_address():
    """
    Get current device MAC address
    """
    import json
    #/sys/class/uio/uio0/device
    print(123)

    config = json.load(open("pipeline_config.json", "r"))
    cmd = 'cat /sys/class/net/{}/address'.format(config["interface"])
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    if res.returncode == 0:
        add = res.stdout.decode().strip()
        return add
    else:
        # not found
        return None


def get_hostname():
    cmd = 'hostname'
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    if res.returncode == 0:
        name = res.stdout.decode().strip()
        return name
    else:
        return None


def get_pci_address(iface):
    """
    Get PCI address for the given interface name
    """
    # ethtool -i iface # this should give enough information
    raise Exception('Not implemented yet')


def get_current_node_info(file_path):
    # mac = get_mac_address()
    hostname = get_hostname()
    found = False
    t_key = None
    t_value = None
    with open(file_path) as f:
      config = yaml.load(f, Loader=yaml.FullLoader)
      for key in config.keys():
        item = config[key]
        if item['hostname'].strip() == hostname:
          t_key = key
          t_value = config[key]
          found = True
          break

      if not found:
        return (None, None)

    return (t_key, t_value)


def get_full_config(file_path):
    config = None
    with open(file_path) as f:
      config = yaml.load(f, Loader=yaml.FullLoader)
    return config

