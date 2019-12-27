# Installation Guide for the Orion-LD Functional Test Suite on CentOS 7

The functional test suite for Orion-LD, found in the Orion-LD repository, under `test/functionalTests`, needs some installation in order to work.
Especially, a python script (`scripts/accumulator-server.py`) is used as receptor of notifications to exercise that part and a few packages need to be installed 
for the accumulator.

1. Install **Netcat** and **Basic Calculator** for CentOS 7:

```bash
# Netcat
yum install nc

# Basic Calculator
yum install bc

```

2. Install **OpenSSL** for Python:

```bash
# First of all add the *EPEL Repository. Because Pip is not available in CentOS 7 core repositories, so you need to enable the EPEL repository.
yum install epel-release

yum install python-pip

# Finally install OpenSSL for Python
pip install pyopenssl

# Update pip
pip install -U pip
pip install -U virtualenv
```

* The EPEL (Extra Packages for Enterprise Linux) repository provides additional software packages that are not included in the standard Red Hat and CentOS repositories.

3. Install **Flask**:

```bash
pip install Flask
```

That should be all for the accumulator.
Now, the test script (`test/functionalTest/testHarness.sh`) needs to *find* the accumulator, to be able to start it:

```bash
export PATH=$PATH:$PWD/scripts/
which accumulator-server.py
```

The output of the `which` command should be:

```text
~/git/context.Orion-LD/scripts/accumulator-server.py
```

That should do it!
Test it by launching:
```bash
test/functionalTest/testHarness.sh
```

There are over 1250 test cases (each with a number of steps), so, it will take a while.
Orion-LD has inherited the functional test suite from `orion` and added some 250 test cases only for NGSI-LD
If you want to run only the NGSi-LD test cases, run the suite with the `-ld` option:

```bash
test/functionalTest/testHarness.sh -ld
```

There are lots of command line options for the test suite; use the `-u` option to see all of them.

In case some functional test case fails, normally due to installation error, and you're not really interested in that case (might be for IPv6 and you aren't interested in IPv6),
you can disable test cases.

Functional test cases can be disabled by exporting an environment variable called `CB_SKIP_FUNC_TESTS`.
For example, to disable the test case 'direct_https_notifications.test' (you don't want https notification, so ...), do this:

```bash
export CB_SKIP_FUNC_TESTS=0706_direct_https_notifications/direct_https_notifications.test
```
Note that not only the name of the test case file, but also the directoy where it resides is part of the "identifier".
This is so, because different functional test case directories can have test case files with the same name.

FYI: after following myself the instructions in the installation guides, the following functional tests fail for me:

* 0706_direct_https_notifications/direct_https_notifications.test
* 0706_direct_https_notifications/direct_https_notifications_no_accept_selfsigned.test

Hmmm, something about https seems to be missing in the instructions ...

Looking closer at the errors, it seems clear that it is the accumulator script that is having problems (the accumulator log-file says: **AttributeError: 'Context' object has no attribute 'wrap_socket'**).
I will have to look into this, but for now, I simply do this:

```bash
export CB_SKIP_FUNC_TESTS="0706_direct_https_notifications/direct_https_notifications.test 0706_direct_https_notifications/direct_https_notifications_no_accept_selfsigned.test"
```

Remember to put this environment variable in your startup files (e.g. `~/.bash_profile`), so that you don't lose it.

If doesn't work the command above, run this command:

```bash
test/functionalTest/testHarness.sh --fromIx 533 --toIx 540
```