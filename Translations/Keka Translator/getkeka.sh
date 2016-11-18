echo "Getting Keka latest version..."

url="http://download.kekaosx.com/?zip"
temp="$1"
pathResources="${0%/*}"
pathApp=${pathResources%/*/*}
app="Keka Translator"

cd "$pathResources"

echo "Launching daemon to open $app again..."
./reopen.sh "$pathResources" "$pathApp" "$app" &

echo "Downloading Keka latest version..."
curl -o "Keka.zip" -L "$url"
echo "Extracting Keka.app..."
unzip Keka.zip
rm Keka.zip
touch updating
exit