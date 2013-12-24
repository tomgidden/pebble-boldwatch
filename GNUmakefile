build: build/boldwatch-2.pbw

run: build/boldwatch-2.pbw
	pebble install --logs

install: build/boldwatch-2.pbw
	pebble install

clean:
	pebble clean
	rm src/js/pebble-js-app.js

build/boldwatch-2.pbw: src/js/pebble-js-app.js src/boldwatch.c appinfo.json
	pebble build

src/js/pebble-js-app.js: src/js/pebble-js-app.src.js resources/configuration.html
	perl -pe 'BEGIN { local $$/; open $$fh,pop @ARGV or die $$!; $$f = <$$fh>; $$f =~ s/\047/\\\047/g; } s/_HTMLMARKER_/$$f/g;' $^ | /usr/local/share/npm/bin/uglifyjs -c -m > $@

