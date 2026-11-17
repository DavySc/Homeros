# Package 16 (org), build A.  Until a build wins and thread 0 moves them,
# the header lives here and so does the oracle target (thread 0's files to
# move: src/org/org.h to include/, the target below to the Makefile).
CPPFLAGS += -Isrc/org

# Org's own parser (org-element in Emacs, from PATH) against src/org/: ORG_N
# random org texts and ORG_N mutations of design/org/synthetic-agenda.org,
# element by element and by position
ORG_N ?= 200
org-oracle: | build
	ORG_N=$(ORG_N) bash src/org/oracle/check.sh $(SEED)
check: org-oracle
.PHONY: org-oracle
