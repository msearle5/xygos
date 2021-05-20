MKPATH=mk/
include $(MKPATH)buildsys.mk

SUBDIRS = src lib
CLEAN = config.status config.log *.dll *.exe

.PHONY: tests dist
tests:
	$(MAKE) -C src tests

TAG = xygos-`cd scripts && ./version.sh`(only will have a visible effect if "git rev-parse" fails and then attempt to get the version from the version stamp file).
OUT = $(TAG).tar.gz

manual:
	echo "To make the manual, please enter docs/ and run `make html` after installing Sphinx."

dist:
	git checkout-index --prefix=$(TAG)/ -a
	scripts/version.sh > $(TAG)/version
	$(TAG)/autogen.sh
	rm -rf $(TAG)/autogen.sh $(TAG)/autom4te.cache
	tar --exclude .gitignore --exclude *.dll --exclude .github \
		--exclude .travis.yml -czvf $(OUT) $(TAG)
	rm -rf $(TAG)
