PIPENV_VERSION=2020.8.13

UNAME = $(shell uname -s)
ARCHITECTURE = $(shell uname -m)

ifndef PYTHON
	# brew doesn't install python@3.9 into /usr/bin/ or similar as of yet,
	# so we set up the direct path to the executable here.
	ifeq ($(UNAME),Darwin)
		ifeq ($(ARCHITECTURE),arm64)
			PYTHON = /opt/homebrew/opt/python@3.9/bin/python3
		else
			PYTHON = /usr/local/opt/python@3.9/bin/python3
		endif
	else
		PYTHON = python3.9
	endif
endif

ifndef INSTALL_PIPENV
	INSTALL_PIPENV = $(PYTHON) -m pip install --user 'pipenv==$(PIPENV_VERSION)' --no-cache-dir
endif
ifndef PIPENV
	PIPENV = PIPENV_VENV_IN_PROJECT=1 PIPENV_HIDE_EMOJIS=true $(PYTHON) -m pipenv
endif


VENV = .venv

.PHONY: install
install:
	PIPENV_VENV_IN_PROJECT=1 /usr/bin/env bash -c "$(PIPENV) --python=$(PYTHON) && $(VENV)/bin/pip install -r <($(PIPENV) lock --requirements)"

.PHONY: pipenv-update
pipenv-update:
	$(PIPENV) update

.PHONY: shell
shell:
	PIPENV_SHELL=$$SHELL $(PIPENV) shell --fancy
