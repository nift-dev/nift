#!/usr/bin/env python3
"""Exercise diagnostic failures independently of incremental core behavior."""
import argparse, pathlib, subprocess, tempfile, unittest
from unittest.mock import patch
import randomized_incremental_differential as harness

p=argparse.ArgumentParser();p.add_argument('--nift',required=True);a=p.parse_args()
binary=str(pathlib.Path(a.nift).resolve())

class Diagnostics(unittest.TestCase):
 def failure(self,action,step,history,retain=None):
  with self.assertRaises(harness.CampaignFailure) as raised:action(retain)
  failure=raised.exception
  self.assertEqual(failure.context['mode'],'hash')
  self.assertEqual(failure.context['seed'],411)
  self.assertEqual(failure.context['step'],step)
  self.assertEqual(failure.context['history'],history)
  self.assertIsNotNone(failure.__cause__)
  self.assertNotIn('UnboundLocalError',str(failure))
  return failure
 def campaign(self,retain=None):return harness.campaign(binary,[411],2,['hash'],retain,['content','touch'])
 def test_before_first_operation(self):
  original=ValueError('before-first marker')
  with patch.object(harness,'setup',side_effect=original):
   failure=self.failure(self.campaign,None,[])
  self.assertIs(failure.__cause__,original)
 def build_failure(self,number,step,history):
  real_run=subprocess.run;seen=0
  def failing_run(*args,**kw):
   nonlocal seen
   if args[0][0]==binary and args[0][1]=='build':
    seen+=1
    if seen==number:return subprocess.CompletedProcess(args[0],1,'original failure marker','fixture failure')
   return real_run(*args,**kw)
  with patch.object(harness.subprocess,'run',side_effect=failing_run):
   failure=self.failure(self.campaign,step,history)
  self.assertIn('original failure marker',str(failure))
 def test_first_operation(self):self.build_failure(2,0,['content'])
 def test_later_operation(self):self.build_failure(4,1,['content','touch'])
 def test_retention_failure_preserves_original(self):
  with tempfile.TemporaryDirectory() as td:
   with patch.object(harness,'setup',side_effect=ValueError('original setup marker')),patch.object(harness.shutil,'copytree',side_effect=OSError('retention marker')):
    failure=self.failure(self.campaign,None,[],td)
   self.assertIn('original setup marker',str(failure))
   self.assertIn('retention marker',failure.context['retention_error'])
 def test_normal_success_with_selected_deck(self):
  result=self.campaign()
  self.assertTrue(result['passed'])
  self.assertEqual(result['operations'],2)
  self.assertEqual(result['clean_oracles'],2)
  self.assertEqual(result['cases'][0]['operations'],['content','touch'])

unittest.main(argv=['randomized_diagnostics_test'],verbosity=2)
