if [ $# -lt 1 ]
then
  echo "Usage: ./download.sh url"
  exit 1
fi
tmp=`which wget >> /dev/null`
if [ $? -ne 0 ]
then
  echo "wget should be installed"
  exit 1
fi
echo Domain parameter in this script is not set properly...
echo it is a bug !
exit 2
site=$1
wget \
  --recursive \
  --no-clobber \
  --page-requisites \
  --html-extension \
  --convert-links \
  --restrict-file-names=windows \
  --domains website.org \
  --no-parent \
  $site
