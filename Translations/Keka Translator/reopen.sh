pathResources="${0%/*}"
pathApp=${pathResources%/*/*}
app="Keka Translator"

count=0
while pgrep $app > /dev/null; do
	count=$(($count +1))
	touch /Users/aone/Desktop/test/$count
	sleep 1
done

echo "Reopening $app..."
open "$pathApp"
exit