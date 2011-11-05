NULL =

SPEC_XML_FILES =			\
	mate-screensaver.xml		\
	dbus-interface.xml		\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(DOCDIR)/spec
htmldoc_DATA = mate-screensaver.html

mate-screensaver.html : dbus-interface.xml
	$(XMLTO) html-nochunks -m config.xsl mate-screensaver.xml

endif # DOCBOOK_DOCS_ENABLED

EXTRA_DIST =				\
	mate-screensaver.xml.in	\
	config.xsl			\
	docbook.css			\
	$(SPEC_XML_FILES)
	$(NULL)

clean-local:
	rm -f *~
	rm -f mate-screensaver.html
