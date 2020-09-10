import sys
import subprocess
import yaml


def get_mac_address():
    """
    Get current device MAC address
    """
    res = subprocess.run('cat /sys/class/net/eno50/address', shell=True, stdout=subprocess.PIPE)
    add = res.stdout.decode().strip()
    return add


def get_pci_address(iface):
    """
    Get PCI address for the given interface name
    """
    # ethtool -i iface # this should give enough information
    raise Exception('Not implemented yet')


def get_current_node_info(file_path):
    mac = get_mac_address()
    found = False
    with open(file_path) as f:
      config = yaml.load(f, Loader=yaml.FullLoader)
      for key in config.keys():
        item = config[key]
        if item['mac'] == mac:
          found = True
          break
      if not found:
        return (None, None)
    return (key, item)


def get_full_config(file_path):
    config = None
    with open(file_path) as f:
      config = yaml.load(f, Loader=yaml.FullLoader)
    return config

