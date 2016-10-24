#!/usr/bin/python
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing cryptography functions using extended commands."""

from __future__ import print_function

import struct
import xml.etree.ElementTree as ET

import subcmd
import utils

# Basic crypto operations
DECRYPT = 0
ENCRYPT = 1

def get_attribute(tdesc, attr_name, required=True):
  """Retrieve an attribute value from an XML node.

  Args:
    tdesc: an Element of the ElementTree, a test descriptor containing
      necessary information to run a single encryption/description
      session.
    attr_name: a string, the name of the attribute to retrieve.
    required: a Boolean, if True - the attribute must be present in the
      descriptor, otherwise it is considered optional
  Returns:
    The attribute value as a string (ascii or binary)
  Raises:
    subcmd.TpmTestError: on various format errors, or in case a required
      attribute is not found, the error message describes the problem.

  """
  # Fields stored in hex format by default.
  default_hex = ('cipher_text', 'iv', 'key')

  data = tdesc.find(attr_name)
  if data is None:
    if required:
      raise subcmd.TpmTestError('node "%s" does not have attribute "%s"' %
                        (tdesc.get('name'), attr_name))
    return ''

  # Attribute is present, does it have to be decoded from hex?
  cell_format = data.get('format')
  if not cell_format:
    if attr_name in default_hex:
      cell_format = 'hex'
    else:
      cell_format = 'ascii'
  elif cell_format not in ('hex', 'ascii'):
    raise subcmd.TpmTestError('%s:%s, unrecognizable format "%s"' %
                      (tdesc.get('name'), attr_name, cell_format))

  text = ' '.join(x.strip() for x in data.text.splitlines() if x)
  if cell_format == 'ascii':
    return text

  # Drop spaces from hex representation.
  text = text.replace(' ', '')
  if len(text) & 3:
    raise subcmd.TpmTestError('%s:%s %swrong hex number size' %
                      (tdesc.get('name'), attr_name, utils.hex_dump(text)))
  # Convert text to binary
  value = ''
  for x in range(len(text)/8):
    try:
      value += struct.pack('<I', int('0x%s' % text[8*x:8*(x+1)], 16))
    except ValueError:
      raise subcmd.TpmTestError('%s:%s %swrong hex value' %
                        (tdesc.get('name'), attr_name, utils.hex_dump(text)))
  return value


class CryptoD(object):
  """A helper object to contain an encryption scheme description.

  Attributes:
    subcmd: a 16 bit max integer, the extension subcommand to be used with
      this encryption scheme.
    sumbodes: an optional dictionary, the keys are strings, names of the
      encryption scheme submodes, the values are integers to be included in
      the appropriate subcommand fields to communicat the submode to the
      device.
  """

  def __init__(self, subcommand, submodes=None):
    self.subcmd = subcommand
    if not submodes:
      submodes = {}
    self.submodes = submodes

SUPPORTED_MODES = {
    'AES': CryptoD(subcmd.AES, {
        'ECB': 0,
        'CTR': 1,
        'CBC': 2,
        'GCM': 3
    }),
}

def crypto_run(node_name, op_type, key, iv, in_text, out_text, tpm):
  """Perform a basic operation(encrypt or decrypt).

  This function creates an extended command with the requested parameters,
  sends it to the device, and then compares the response to the expected
  value.

  Args:
    node_name: a string, the name of the XML node this data comes from. The
      format of the name is "<enc type>:<submode> ....", where <enc type> is
      the major encryption mode (say AED or DES) and submode - a variant of
      the major scheme, if exists.

   op_type: an int, encodes the operation to perform (encrypt/decrypt), passed
     directly to the device as a field in the extended command
   key: a binary string
   iv: a binary string, might be empty
   in_text: a binary string, the input of the encrypt/decrypt operation
   out_text: a binary string, might be empty, the expected output of the
     operation. Note that it could be shorter than actual output (padded to
     integer number of blocks), in which case only its length of bytes is
     compared debug_mode: a Boolean, if True - enables tracing on the console
   tpm: a TPM object to send extended commands to an initialized TPM

  Returns:
    The actual binary string, result of the operation, if the
    comparison with the expected value was successful.

  Raises:
    subcmd.TpmTestError: in case there were problems parsing the node name, or
      verifying the operation results.

  """
  mode_name, submode_name = node_name.split(':')
  submode_name = submode_name[:3].upper()

  mode = SUPPORTED_MODES.get(mode_name.upper())
  if not mode:
    raise subcmd.TpmTestError('unrecognizable mode in node "%s"' % node_name)

  submode = mode.submodes.get(submode_name, 0)
  cmd = '%c' % op_type    # Encrypt or decrypt
  cmd += '%c' % submode   # A particular type of a generic algorithm.
  cmd += '%c' % len(key)
  cmd += key
  cmd += '%c' % len(iv)
  if iv:
    cmd += iv
  cmd += struct.pack('>H', len(in_text))
  cmd += in_text
  if tpm.debug_enabled():
    print('%d:%d cmd size' % (op_type, mode.subcmd),
          len(cmd), utils.hex_dump(cmd))
  wrapped_response = tpm.command(tpm.wrap_ext_command(mode.subcmd, cmd))
  real_out_text = tpm.unwrap_ext_response(mode.subcmd, wrapped_response)
  if out_text:
    if len(real_out_text) > len(out_text):
      real_out_text = real_out_text[:len(out_text)]  # Ignore padding
    if real_out_text != out_text:
      if tpm.debug_enabled():
        print('Out text mismatch in node %s:\n' % node_name)
      else:
        raise subcmd.TpmTestError(
          'Out text mismatch in node %s, operation %d:\n'
          'In text:%sExpected out text:%sReal out text:%s' % (
            node_name, op_type,
            utils.hex_dump(in_text),
            utils.hex_dump(out_text),
            utils.hex_dump(real_out_text)))
  return real_out_text


def crypto_test(tdesc, tpm):
  """Perform a single test described in the xml file.

  The xml node contains all pertinent information about the test inputs and
  outputs.

  Args:
    tdesc: an Element of the ElementTree, a test descriptor containing
      necessary information to run a single encryption/description
      session.
    tpm: a TPM object to send extended commands to an initialized TPM
  Raises:
    subcmd.TpmTestError: on various execution errors, the details are included
      in the error message.

  """
  node_name = tdesc.get('name')
  key = get_attribute(tdesc, 'key')
  if len(key) not in (16, 24, 32):
    raise subcmd.TpmTestError('wrong key size "%s:%s"' % (
        node_name,
        ''.join('%2.2x' % ord(x) for x in key)))
  iv = get_attribute(tdesc, 'iv', required=False)
  if iv and len(iv) != 16:
    raise subcmd.TpmTestError('wrong iv size "%s:%s"' % (
        node_name,
        ''.join('%2.2x' % ord(x) for x in iv)))
  clear_text = get_attribute(tdesc, 'clear_text')
  if tpm.debug_enabled():
    print('clear text size', len(clear_text))
  cipher_text = get_attribute(tdesc, 'cipher_text', required=False)
  real_cipher_text = crypto_run(node_name, ENCRYPT, key, iv,
                                clear_text, cipher_text, tpm)
  crypto_run(node_name, DECRYPT, key, iv, real_cipher_text,
             clear_text, tpm)
  print(utils.cursor_back() + 'SUCCESS: %s' % node_name)

def crypto_tests(tpm, xml_file):
  tree = ET.parse(xml_file)
  root = tree.getroot()
  for child in root:
    crypto_test(child, tpm)
