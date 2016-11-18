url="http://download.kekaosx.com/?zip"
temp="$1"
pathResources="${0%/*}"
pathApp=${pathResources%/*/*}
app="Keka Translator"

echo "Downloading Keka latest version..."
cd "$pathResources"
curl -o "Keka.zip" -L "$url"
unzip Keka.zip
rm Keka.zip

#while pgrep $app > /dev/null; do
#sleep 1
#done

echo "Reopening $app..."
open $pathApp
exit