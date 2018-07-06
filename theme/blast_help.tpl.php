<?php
/**
 * This template displays the help page for the BLAST UI
 */
?>

<h3>Module Description</h3>
<p>This module provides a basic interface to allow your users to utilize your server's NCBI BLAST+.</p>

<h3>Setup Instructions</h3>
<ol>
<li>Install NCBI BLAST+ on your server (Tested with 2.2.26+). There is a <a href="https://launchpad.net/ubuntu/+source/ncbi-blast+">package available for Ubuntu</a> to ease installation. Optionally you can set the path to your BLAST executable <a href="<?php print url('admin/tripal/extension/tripal_blast_ext/blast_ui_ext');?>">in the settings</a>.</li>
<li>Optionally, create Tripal External Database References to allow you to link the records in your BLAST database to further information. To do this simply go to <a href="<?php print url('admin/tripal/chado/tripal_db/add'); ?>" target="_blank">Tripal > Chado Modules > Databases > Add DB</a> and make sure to fill in the Database prefix which will be concatenated with the record IDs in your BLAST database to determine the link-out to additional information. Note that a regular expression can be used when creating the BLAST database to indicate what the ID is.</li>
<li><a href="<?php print url('node/add/blastdb');?>">Create "Blast Database" nodes</a> for each dataset you want to make available for your users to BLAST against. BLAST databases should first be created using the command-line <code>makeblastdb</code> program with the <code>-parse_seqids</code> flag.</li>
<li>It's recommended that you also install the <a href="http://drupal.org/project/tripal_daemon">Tripal Job Daemon</a> to manage BLAST jobs and ensure they are run soon after being submitted by the user. Without this additional module, administrators will have to execute the tripal jobs either manually or through use of cron jobs.</li>
</ol>

<h3>Highlighted Functionality</h3>
<ul>
  <li>Supports <a href="<?php print url('blast/nucleotide/nucleotide');?>">blastn</a>, 
    <a href="<?php print url('blast/nucleotide/protein');?>">blastx</a>, 
    <a href="<?php print url('blast/protein/protein');?>">blastp</a> and 
    <a href="<?php print url('blast/protein/nucleotide');?>">tblastx</a> with separate forms depending upon the database/query type.</li>
  <li>Simple interface allowing users to paste or upload a query sequence and then select from available databases. Additionally, a FASTA file can be uploaded for use as a database to BLAST against (this functionality can be disabled).</li>
  <li>Tabular Results listing with alignment information and multiple download formats (HTML, TSV, XML) available.</li>
  <li>Completely integrated with <a href="<?php print url('admin/tripal/tripal_jobs');?>">Tripal Jobs</a> providing administrators with a way to track BLAST jobs and ensuring long running BLASTs will not cause page time-outs</li>
  <li>BLAST databases are made available to the module by <a href="<?php print url('node/add/blastdb');?>">creating Drupal Pages</a> describing them. This allows administrators to <a href="<?php print url('admin/structure/types/manage/blastdb/fields');?>">use the Drupal Field API to add any information they want to these pages</a>.</li>
  <li>BLAST database records can be linked to an external source with more information (ie: NCBI) per BLAST database.</li>
</ul>

<h3>Added by NCGAS - link to JBrowse</h3>
There are two things that need to be configured - the link to the jbrowse and the association of the blast databases with the correct jbrowse (if there are multiple).  These are changes made to the /tripal_ui_ext/theme/blast_report.tpl.php file.<br>
<h4>Configuring the jbrowse link:<br></h4>
Under “ADD JBROWSE LINK HERE”, adjust the variable to hold your jbrowse link, but not including the folder.  <br>
<br>
For example, if we have three jbrowse hosted (test1, test2, and test3), we have three links:<br>
http://oururl.org/jbrowse/index.html?data=test1&loc…<br>
http://oururl.org/jbrowse/index.html?data=test2&loc…<br>
http://oururl.org/jbrowse/index.html?data=test3&loc…<br>

in this case, include only “http://oururl.org/jbrowse/index.html?data=“<br>
<br>
Another example is using the default jbrowse location for a single browser, in which case you may have the following link:<br>
http://oururl.org/jbrowse/index.html?loc=…<br>
<br>
in this case, you can still use the “http://oururl.org/jbrowse/index.html?data=“ as “data” is the default folder.<br>

<h4>Configure the association between the blast databases and the correct jbrowse:</h4>
<ul>
<li>If you are only using one reference, and it is in the /jbrowse/data default, you can leave this part alone.  All the output links will default to the correct place.
<li>If you are using only one reference, and it isn’t in the /jbrowse/data default, you can simply change the line “$jb=data” to “$jb=<whatever your directory is>” and it will default all the links to the correct jbrowse.
<li>If you are running parallel instances of browsers with different blast databases associated with them, you will have to configure the following:
Under “MAP JBROWSE DATABASES HERE”:<br>
There is an if block:<br>
   if ($db==“Test1 - Nucleotide")<br>
        $jb=“test1";<br>
<br>
This links the database used to the correct jbrowse, in this case Test1 - Nucleotide (the name set up as a blast db) to the jbrowse instance housed in /jbrowse/test1. <br>
Fill this out for each database, i.e.:<br>
   if ($db==“Test1 - Nucleotide")<br>
        $jb=“test1";<br>
   if ($db==“Test1 - Protein”)<br>
        $jb=“test1";<br>
   if ($db==“Test2 - Nucleotide")<br>
        $jb=“test2”;<br>
   …<br>




