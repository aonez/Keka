pathResources="$1"
pathApp="$2"
app="$3"

while [ ! -f "$pathResources/updating" ]
do
	sleep 1
done

rm "$pathResources/updating"
#sleep 3

echo "Reopening $app..."
open "$pathApp"

exit