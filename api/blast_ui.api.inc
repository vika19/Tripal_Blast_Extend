<?php

/**
 * @file
 * Contains more generally applicable functions as well as some meant to help developers
 * Plug-in to the BLAST UI functionality
 */

/**
 * Get a specific BlastDB.
 *
 * @param $identifiers
 *   An array of identifiers used to determine which BLAST DB to retrieve.
 *
 * @return
 *   A fully-loaded BLAST DB Node
 */
function get_blast_database($identifiers) {
  $node = FALSE;

  if (isset($identifiers['nid'])) {
    $node = node_load($identifiers['nid']);
  }
  elseif (isset($identifiers['name'])) {

    $nid = db_query('SELECT nid FROM {blastdb} WHERE name=:name', array(':name' => $identifiers['name']))->fetchField();
    $node = node_load($nid);

  } elseif (isset($identifiers['path'])) {

    $nid = db_query('SELECT nid FROM {blastdb} WHERE path LIKE :path', array(':path' => db_like($identifiers['path']) . '%'))->fetchField();
    $node = node_load($nid);

  }

  return $node;
}

/**
 * Returns a list BLAST DATABASE options
 *
 * @param $type
 *   The type of BLAST dabases to restrict the list to (ie: n: nucleotide or p: protein)
 *
 * @return
 *   An array where the nid is the key and the value is the human-readable name of the option
 */
function get_blast_database_options($type) {
  global $user;

  // Use the Entity API to get a list of BLAST Nodes to load
  // We use this function in order respect node access control so that
  // administrators can use this module in combination with a node access module
  // of their choice to limit access to specific BLAST databases.
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'node')
    // Restrict to BLASTDB nodes.
    ->entityCondition('bundle', 'blastdb')
    // Restrict to Published nodes.
    ->propertyCondition('status', 1)
    // Restrict to nodes the current user has permission to view.
    ->addTag('node_access');
  $entities = $query->execute();


  // Get all BlastDB nodes
  $nodes  = node_load_multiple(array_keys($entities['node']));

  // Support obsolete database type n/p
  $obs_type = '';
  if ($type == 'protein') {
    $obs_type = 'p';
  }
  else {
    $obs_type = 'n';
  }

  $options = array();
  foreach ($nodes as $node) {
    if ( isset($node) && isset($node->db_dbtype) ) {
      if ( ($node->db_dbtype == $type) OR ($node->db_dbtype == $obs_type) ) {
        $options[$node->nid] = $node->db_name;
      }
    }
  }

  asort($options);
  $options[0] = 'Select a Dataset';

  return $options;
}

/**
 * Retrieve all the information for a blast job in a standardized node-like format.
 *
 * @param $job_id
 *   The non-encoded tripal job_id.
 * @retun
 *   An object describing the blast job.
 */
function get_BLAST_job($job_id) {

  $blastjob = db_query('SELECT * FROM blastjob WHERE job_id=:id', array(':id' => $job_id))->fetchObject();
  $tripal_job = tripal_get_job($job_id);

  $job = new stdClass();
  $job->job_id = $job_id;
  $job->program = $blastjob->blast_program;
  $job->options = unserialize($blastjob->options);
  $job->date_submitted = $tripal_job->submit_date;
  $job->date_started = $tripal_job->start_time;
  $job->date_completed = $tripal_job->end_time;

  // TARGET BLAST DATABASE.
  // If a provided blast database was used then load details.
  if ($blastjob->target_blastdb ) {
    $job->blastdb = get_blast_database(array('nid' => $blastjob->target_blastdb));
  }
  // Otherwise the user uploaded their own database so provide what information we can.
  else {
    $job->blastdb = new stdClass();
    $job->blastdb->db_name = 'User Uploaded';
    $job->blastdb->db_path = $blastjob->target_file;
    $job->blastdb->linkout = new stdClass();
    $job->blastdb->linkout->none = TRUE;

    if ($job->program == 'blastp' OR $job->program == 'tblastn') {
      $job->blastdb->db_dbtype = 'protein';
    }
    else {
      $job->blastdb->db_dbtype = 'nucleotide';
    }
  }

  // FILES.
  $job->files = new stdClass();
  $job->files->query = $blastjob->query_file;
  $job->files->target = $blastjob->target_file;
  $job->files->result = new stdClass();
  $job->files->result->archive = $blastjob->result_filestub . '.asn';
  $job->files->result->xml = $blastjob->result_filestub . '.xml';
  $job->files->result->tsv = $blastjob->result_filestub . '.tsv';
  $job->files->result->html = $blastjob->result_filestub . '.html';
  $job->files->result->gff = $blastjob->result_filestub . '.gff';

  return $job;
}

/**
 * Run BLAST (should be called from the command-line)
 *
 * @param $program
 *   Which BLAST program to run (ie: 'blastn', 'tblastn', tblastx', 'blastp','blastx')
 * @param $query
 *   The full path and filename of the query FASTA file
 * @param $database
 *   The full path and filename prefix (excluding .nhr, .nin, .nsq, etc.)
 * @param $output_filestub
 *   The filename (not including path) to give the results. Should not include file type suffix
 * @param $options
 *   An array of additional option where the key is the name of the option used by
 *   BLAST (ie: 'num_alignments') and the value is relates to this particular
 *   BLAST job (ie: 250)
 */
function run_BLAST_tripal_job($program, $query, $database, $output_filestub, $options, $job_id = NULL) {

  $output_file = $output_filestub . '.asn';
  $output_file_xml = $output_filestub . '.xml';
  $output_file_tsv = $output_filestub . '.tsv';
  $output_file_html = $output_filestub . '.html';
  $output_file_gff = $output_filestub . '.gff';

  print "\nExecuting $program\n\n";
  print "Query: $query\n";
  print "Database: $database\n";
  print "Results File: $output_file\n";

  print "Options:\n";

  // Allow administrators to use an absolute path for these commands.
  // Defaults to using $PATH.
  $blast_path    = variable_get('blast_path', '');
  $blast_threads = variable_get('blast_threads', 1);

  // Strip the extension off the BLAST target
  $database = preg_replace("/(.*)\.[pn]\w\w/", '$1', $database);

  // The executables:
  $program       = $blast_path . $program;
  $blast_formatter_command = $blast_path . 'blast_formatter';

  $blast_cmd = "$program -query '$query' -db '$database' -out '$output_file' -outfmt=11";
  if (!empty($options)) {
    foreach ($options as $opt => $val) {
      if ($val) {
        print "\t$opt: $val\n";
        $blast_cmd .= " -$opt $val";
      }
    }
  }

 // Setting the value of threads by admin page
 $blast_cmd .= " -num_threads ".$blast_threads;

  print "\nExecuting the following BLAST command:\n" . $blast_cmd . "\n";

  system($blast_cmd);

  if (!file_exists($output_file)) {
    tripal_report_error(
      'blast_ui',
      TRIPAL_ERROR,
      "BLAST did not complete successfully as is implied by the lack of output file (%file). The command run was @command",
      array('%file' => $output_file, '@command' => $blast_cmd),
      array('print' => TRUE)
    );
    return FALSE;
  }

  print "\nGenerating additional download formats...\n";

  print "\tXML\n";
  $format_cmd = "$blast_formatter_command -archive $output_file -outfmt 5 -out $output_file_xml";
  print "\nExecuting $format_cmd\n\n";
  system($format_cmd);
  if (!file_exists($output_file_xml)) {
    tripal_report_error(
      'blast_ui',
      TRIPAL_ERROR,
      "Unable to convert BLAST ASN.1 archive to XML (%archive => %file).",
      array('%archive' => $output_file, '%file' => $output_file_xml),
      array('print' => TRUE)
    );
  }

  print "\tTab-delimited\n";
  system("$blast_formatter_command -archive $output_file -outfmt 7 -out $output_file_tsv");
  if (!file_exists($output_file_tsv)) {
    tripal_report_error(
      'blast_ui',
      TRIPAL_WARNING,
      "Unable to convert BLAST ASN.1 archive to Tabular Output (%archive => %file).",
      array('%archive' => $output_file, '%file' => $output_file_tsv),
      array('print' => TRUE)
    );
  }

  print "\tGFF\n";
  convert_tsv2gff3($output_file_tsv,$output_file_gff);

  if (!file_exists($output_file_gff)) {
    tripal_report_error(
      'blast_ui',
      TRIPAL_WARNING,
      "Unable to convert BLAST Tabular Output to GFF Output (%archive => %file).",
      array('%archive' => $output_file, '%file' => $output_file_gff),
      array('print' => TRUE)
    );
  }

  print "\tHTML (includes alignments)\n";
  system("$blast_formatter_command -archive $output_file -outfmt 0 -out $output_file_html -html");
  if (!file_exists($output_file_tsv)) {
    tripal_report_error(
      'blast_ui',
      TRIPAL_WARNING,
      "Unable to convert BLAST ASN.1 archive to HTML Output (%archive => %file).",
      array('%archive' => $output_file, '%file' => $output_file_html),
      array('print' => TRUE)
    );
  }


  print "\nDone!\n";
}

/**
 * FASTA validating parser
 *
 * A sequence in FASTA format begins with a single-line description, followed
 * by lines of sequence data.The description line is distinguished from the
 * sequence data by a greater-than (">") symbol in the first column. The word
 * following the ">" symbol is the identifier of the sequence, and the rest of
 * the line is the description (both are optional). There should be no space
 * between the ">" and the first letter of the identifier. The sequence ends
 * if another line starting with a ">" appears which indicates the start of
 * another sequence.
 *
 * @param $type
 *   The type of sequence to be validated (ie: either nucleotide or protein).
 * @param $sequence
 *  A string of characters to be validated.
 *
 * @return
 *  Return a boolean. 1 if the sequence does not pass the format valifation stage and 0 otherwise.
 *
 */
function validate_fasta_sequence($type, $sequence) {

  if ($type == 'nucleotide') {
    $fastaIdRegEx = '/^>.*(\\n|\\r)/';
    $fastaSeqRegEx = '/[^ATCGNUKMBVSWDYRHatcgnukmbvswdyrh\n\r]/'; //Includes IUPAC codes.
    if ( preg_match($fastaSeqRegEx,$sequence) && !(preg_match($fastaIdRegEx,$sequence)) ) {
      return TRUE;
    } else {
      return FALSE;
    }
  } elseif ($type == 'protein') {
    $fastaIdRegEx = '/^>.*(\\n|\\r)/';
    $fastaSeqRegEx = '/[^ABCDEFGHIKLMNPQRSTUVWYZXabcdefghiklmnpqrstuvwyzx\*\-\n\r]/';
    if ( preg_match($fastaSeqRegEx,$sequence) && !(preg_match($fastaIdRegEx,$sequence)) ) {
      return TRUE;
    } else {
      return FALSE;
    }
  }
  return FALSE;
}

/**
 * Retrieve the regex to capture the Link-out Accession from the Hit Def.
 *
 * @param $nid
 *   The node ID of the BLAST database the hit is from.
 * @param $options
 *   An array of options that can be passed to this function. Supported
 *   options include:
 *    -
 *
 * @return
 *   A PHP regex for use with preg_match to cature the Link-out Accession.
 */
function get_blastdb_linkout_regex($node, $options = array()) {

  if (empty($node->linkout->regex)) {
    switch ($node->linkout->regex_type) {
      case 'default':
        $regex = '/^(\S+).*/';
        break;
      case 'genbank':
        $regex = '/^gb\|([^\|])*\|.*/';
        break;
      case 'embl':
        $regex = '/^embl\|([^\|])*\|.*/';
        break;
      case 'swissprot':
        $regex = '/^sp\|([^\|])*\|.*/';
        break;
    }
  }
  else {
    $regex = $node->linkout->regex;
  }

  return $regex;
}

/**
 * Return a list of recent blast jobs to be displayed to the user.
 *
 * @param $programs
 *   An array of blast programs you want jobs to be displayed for (ie: blastn, blastx, tblastn, blastp)
 *
 * @return
 *   An array of recent jobs.
 */
function get_recent_blast_jobs($programs = array()) {

  $filter_jobs = !empty($programs);

  // Retrieve any recent jobs from the session variable.
  if (isset($_SESSION['blast_jobs'])) {

    $jobs = array();
    foreach ($_SESSION['blast_jobs'] as $job_secret) {
      $add = TRUE;

      $job_id = blast_ui_reveal_secret($job_secret);
      $job = get_BLAST_job($job_id);

      // @TODO: Check that the results are still available.
      // This is meant to replace the arbitrary only show jobs executed less than 48 hrs ago.

      // Remove jobs from the list that are not of the correct program.
      if ($filter_jobs AND !in_array($job->program, $programs)) {
        $add = FALSE;
      }

      if ($add) {

        $job->query_summary = format_query_headers($job->files->query);

        $jobs[] = $job;
      }

    }

    return $jobs;
  }
  else {
    return array();
  }
}

/**
 * Retrieve the number of recent jobs.
 */
function get_number_of_recent_jobs() {
  if (isset($_SESSION['blast_jobs'])) {
    return sizeof($_SESSION['blast_jobs']);
  }
  return 0;
}

/**
 * Summarize a fasta file based on it's headers.
 *
 * @param $file
 *   The full path to the FASTA file.
 *
 * @return
 *   A string describing the number of sequences and often including the first query header.
 */
function format_query_headers($file) {

  $headers = array();
  exec('grep ">" '.$file, $headers);

  // Easiest case: if there is only one query header then show it.
  if (sizeof($headers) == 1 AND isset($headers[0])) {
    return ltrim($headers[0], '>');
  }
  // If we have at least one header then show that along with the count of queries.
  elseif (isset($headers[0])) {
    return sizeof($headers) . ' queries including "' . ltrim($headers[0], '>') . '"';
  }
  // If they provided a sequence with no header.
  elseif (empty($headers)) {
    return 'Unnamed Query';
  }
  // At the very least show the count of queries.
  else {
    return sizeof($headers) . ' queries';
  }

}

/**
 * Sort recent blast jobs by the date they were submitted.
 * Ascending is in order of submission.
 *
 * THIS FUNCTION SHOULD BY USED BY USORT().
 */
function sort_blast_jobs_by_date_submitted_asc($a, $b) {
  return ($a->date_submitted - $b->date_submitted);
}

/**
 * Sort recent blast jobs by the date they were submitted.
 * Descending is most recent first.
 *
 * THIS FUNCTION SHOULD BY USED BY USORT().
 */
function sort_blast_jobs_by_date_submitted_desc($a, $b) {
  return ($b->date_submitted - $a->date_submitted);
}

/**
 * Generate an image of HSPs for a given hit.
 *
 * history:
 *    09/23/10  Carson  created
 *    04/16/12  eksc    adapted into POPcorn code
 *    03/12/15  deepak  Adapted code into Tripal BLAST
 *    10/23/15  lacey   Fixed deepak's code to be suitable for Drupal.
 *
 * @param $acc
 *    target name
 * @param $name
 *    query name, false if none
 * @param $tsize
 *    target size
 * @param $qsize
 *    query size
 * @param $hits
 *    each hit represented in URL as: targetstart_targetend_hspstart_hspend;
 * @param $score
 *    score for each hit
 *
 * @returm
 *    A base64 encoded image representing the hit information.
 */
function generate_blast_hit_image($acc = '', $scores, $hits, $tsize, $qsize, $name, $hit_name) {
  $tok = strtok($hits, ";");
  $b_hits = Array();
  while ($tok !== false) {
    $b_hits[] = $tok;
    $tok = strtok(";");
  }

  // extract score information from score param
  $tokscr = strtok($scores, ";");
  $b_scores = Array();
  while ($tokscr !== false) {
   $b_scores[] = $tokscr;
   $tokscr = strtok(";");
  }

  // image measurements
  $height = 200 + (count($b_hits) * 16);
  $width  = 520;

  $img = imagecreatetruecolor($width, $height);

  $white      = imagecolorallocate($img, 255, 255, 255);
  $black      = imagecolorallocate($img, 0, 0, 0);
  $darkgray   = imagecolorallocate($img, 100, 100, 100);
  $strong     = imagecolorallocatealpha($img, 202, 0, 0, 15);
  $moderate   = imagecolorallocatealpha($img, 204, 102, 0, 20);
  $present    = imagecolorallocatealpha($img, 204, 204, 0, 35);
  $weak       = imagecolorallocatealpha($img, 102, 204, 0, 50);
  $gray       = imagecolorallocate($img, 190, 190, 190);
  $lightgray  = $white; //imagecolorallocate($img, 230, 230, 230);

  imagefill($img, 0, 0, $lightgray);

  // Target coordinates
  $maxlength = 300;
  $t_length = ($tsize > $qsize)
                ? $maxlength : $maxlength - 50;
  $q_length = ($qsize > $tsize)
                ? $maxlength : $maxlength - 50;

  $tnormal = $t_length / $tsize;
  $qnormal = $q_length / $qsize;

  $t_ystart = 30;
  $t_yend   = $t_ystart + 20;

  $t_xstart = 50;
  $t_xend   = $t_xstart + $t_length;
  $t_center = $t_xstart + ($t_length / 2);

  // Target labels
  $warn = '"'. $hit_name . '"';
  imagestring($img, 5, $t_xstart, $t_ystart-20, $acc.$warn, $black);
  imagestring($img, 3, 5, $t_ystart+2, "Target", $black);

  // Draw bar representing target
  imagefilledrectangle($img, $t_xstart, $t_ystart, $t_xend, $t_yend, $gray);
  imagerectangle($img, $t_xstart, $t_ystart, $t_xend, $t_yend, $darkgray);

  // query coordinates
  $q_maxheight = 250;
  $q_ystart = $t_yend + 100;
  $q_yend = $q_ystart + 20;

  $q_xstart = $t_center - $q_length / 2;
  $q_xend = $q_xstart + $q_length;

  $q_center = ($q_xend + $q_xstart) / 2;
  $q_xwidth = $q_xend - $q_xstart;

  // Query labels
  imagestring($img, 5, $q_xstart, $q_yend+2, $name, $black);
  imagestring($img, 3, $q_xstart, $q_ystart+2, 'Query', $black);

  // Draw bar representing query
  imagefilledrectangle($img, $q_xstart, $q_ystart, $q_xend, $q_yend, $gray);
  imagerectangle($img ,$q_xstart, $q_ystart, $q_xend, $q_yend, $darkgray);

  // HSP bars will start here
  $hsp_bary = $q_yend + 20;

  // Draw solids for HSP alignments
  for ($ii=count($b_hits)-1; $ii>=0; $ii--) {
    // alignment

   $cur_hit = $b_hits[$ii];
   $cur_score = intval($b_scores[$ii]);

   // set color according to score
   $cur_color = $darkgray;
   if ($cur_score > 200) {
     $cur_color = $strong;
   }
   else if ($cur_score > 80 && $cur_score <= 200) {
     $cur_color = $moderate;
   }
   else if ($cur_score > 50 && $cur_score <= 80) {
     $cur_color = $present;
   }
   else if ($cur_score > 40 && $cur_score <= 50) {
     $cur_color = $weak;
   }

   $t_start = $tnormal *  intval(strtok($cur_hit, "_")) + $t_xstart;
    $t_end = $tnormal *  intval(strtok("_")) + $t_xstart;
    $q_start = $qnormal * intval(strtok("_")) + $q_xstart;
    $q_end = $qnormal *  intval(strtok("_")) + $q_xstart;

    $hit1_array = array($t_start, $t_yend, $t_end, $t_yend, $q_end,
                        $q_ystart, $q_start, $q_ystart);

   // HSP coords
    imagefilledpolygon($img, $hit1_array, 4, $cur_color);

  }//each hit

  // Draw lines over fills for HSP alignments
  for ($ii=0; $ii<count($b_hits); $ii++) {
   // alignment

   $cur_hit = $b_hits[$ii];
   $t_start = $tnormal *  intval(strtok($cur_hit, "_")) + $t_xstart;
    $t_end = $tnormal *  intval(strtok("_")) + $t_xstart;
    $q_start = $qnormal * intval(strtok("_")) + $q_xstart;
    $q_end = $qnormal *  intval(strtok("_")) + $q_xstart;

   $hit1_array = array($t_start, $t_yend, $t_end, $t_yend, $q_end, $q_ystart,
                       $q_start, $q_ystart,);

   imagerectangle($img, $t_start, $t_ystart, $t_end, $t_yend, $black);
   imagerectangle($img, $q_start, $q_ystart, $q_end, $q_yend, $black);
   imagepolygon ($img, $hit1_array, 4, $black);

    // show HSP

   imagestring($img, 3, 2, $hsp_bary, ($acc ."HSP" . ($ii + 1)), $black);

   $cur_score = intval($b_scores[$ii]);

   // set color according to score
   $cur_color = $darkgray;
   if ($cur_score > 200) {
     $cur_color = $strong;
   }
   else if ($cur_score > 80 && $cur_score <= 200) {
     $cur_color = $moderate;
   }
   else if ($cur_score > 50 && $cur_score <= 80) {
     $cur_color = $present;
   }
   else if ($cur_score > 40 && $cur_score <= 50) {
     $cur_color = $weak;
   }

   imagefilledrectangle($img, $q_start, $hsp_bary, $q_end, $hsp_bary+10, $cur_color);
    $hsp_bary += 15;
  }//each hit

  // Draw the key

  $xchart = 390;
  $ychart = 10;
  $fontsize = 4;
  $yinc = 20;
  $ywidth = 7;
  $xinc = 10;

  imagestring($img, 5, $xchart, $ychart - 5, "Bit Scores", $black);

  imagestring($img, $fontsize, $xchart + $yinc + $xinc,$ychart + ($yinc * 1) + $ywidth, ">= 200" , $black);
  imagestring($img, $fontsize, $xchart + $yinc + $xinc,$ychart + ($yinc * 2) + $ywidth, "80 - 200" , $black);
  imagestring($img, $fontsize, $xchart + $yinc + $xinc,$ychart + ($yinc * 3) + $ywidth, "50 - 80" , $black);
  imagestring($img, $fontsize, $xchart + $yinc + $xinc,$ychart + ($yinc * 4) + $ywidth, "40 - 50" , $black);
  imagestring($img, $fontsize, $xchart + $yinc + $xinc,$ychart + ($yinc * 5) + $ywidth, "< 40" , $black);

  imagefilledRectangle($img, $xchart, $ychart + ($yinc * 1) + $xinc, $xchart + $yinc, $ychart + ($yinc * 2), $strong);
  imagefilledRectangle($img, $xchart, $ychart + ($yinc * 2) + $xinc, $xchart + $yinc, $ychart + ($yinc * 3), $moderate);
  imagefilledRectangle($img, $xchart, $ychart + ($yinc * 3) + $xinc, $xchart + $yinc, $ychart + ($yinc * 4), $present);
  imagefilledRectangle($img, $xchart, $ychart + ($yinc * 4) + $xinc, $xchart + $yinc, $ychart + ($yinc * 5), $weak);
  imagefilledRectangle($img, $xchart, $ychart + ($yinc * 5) + $xinc, $xchart + $yinc, $ychart + ($yinc * 6), $darkgray);

  // Now, we have a completed image resource and need to change it to an actual image
  // that can be displayed. This is done using imagepng() but unfortuatly that function
  // either saves the image to a file or outputs it directly to the screen. Thus, we use
  // the following code to capture it and base64 encode it.
  ob_start(); // Start buffering the output
  imagepng($img, null, 0, PNG_NO_FILTER);
  $b64_img = base64_encode(ob_get_contents()); // Get what we've just outputted and base64 it
  imagedestroy($img);
  ob_end_clean();

  return $b64_img;
}

/**
 * Convert tsv blast output to gff output file.
 *
 * Created by Sofia Robb
 * 09/15/2016
 *
 * The subject (hit) will be the source feature.
 * The query will be the target.
 *
 * @todo: find a more efficient way since currently the first loop stores all the blast
 *   results into an array and then the second loop prints them.
 *
 * @param $blast_tsv
 *  The name of the blast tsv output file.
 * @param $blast_gff
 *  The name of the blast gff output file.
 */
function convert_tsv2gff3($blast_tsv,$blast_gff){

  // Open a new file for writting the gff.
  $gff = fopen($blast_gff,"w");
  fwrite($gff,"##gff-version 3\n");

  // Open the TSV file to read from.
  $tsv = fopen($blast_tsv, "r") or die("Unable to open tsv file!");

  // For each line in the TSV file...
  $results = array();
  while(!feof($tsv)) {
    $line = fgets($tsv);
    $line = rtrim($line);

    // Skip the line if it's empty.
    if (preg_match('/^#/',$line) or preg_match('/^\s*$/',$line)){
      continue;
    }

    // Each line has the following parts:
    //  0: query id,
    //  1: subject id,
    //  2: % identity,
    //  3: alignment length,
    //  4: mismatches,
    //  5: gap opens,
    //  6: q. start,
    //  7: q. end,
    //  8: s. start,
    //  9: s. end,
    // 10: evalue,
    // 11: bit score
    $parts = preg_split('/\t/', $line);

    // Assign the important parts of the time to readable variables.
    $hitname = $parts[1];
    $hspInfo = "$parts[0],$parts[8],$parts[9],$parts[6],$parts[7]";
    $eval = $parts[10];
    $results[$hitname][$hspInfo] = $eval;

    $IDs = array();
    $count = 0;
    $last_q = NULL;

    // Need to go thru each line of tsv to find the first and last hsp of a hit.
    // Need to get the smallest and largest coordinate for the parent feature
    foreach ($results as $s => $hspInfoArray) {
      $hsp = 0;
      foreach ($hspInfoArray as $hspInfoStr => $e) {
        list($q,$ss,$se,$qs,$qe) = preg_split('/,/',$hspInfoStr);
        if (!$last_q or  $q != $last_q) {
          $count++;
          $hsp=0;
        }
        $q_strand = '+';
        if ($qs > $qe) {
          list($qs,$qe) = array($qe,$qs);
          $q_strand = '-';
        }
        $IDs["$s,$q"]['strand']='+';
        list($start,$end) = array($ss,$se);
        if($ss > $se) {
          list($start,$end) = array($se,$ss);
          $IDs["$s,$q"]['strand']='-';
        }
        if (!array_key_exists('SS',$IDs["$s,$q"]) or $ss < $IDs["$s,$q"]['SS']) {
          $IDs["$s,$q"]['SS'] = $ss;
        }
        if (!array_key_exists('SE',$IDs["$s,$q"]) or $se > $IDs["$s,$q"]['SE']) {
          $IDs["$s,$q"]['SE'] = $se;
        }
        if (!array_key_exists('E',$IDs["$s,$q"]) or $e < $IDs["$s,$q"]['E']) {
          $IDs["$s,$q"]['E'] = $e;
        }
        $hsp++;
        $IDs["$s,$q"]['HSPs'][] = join("\t", array($s, "BLASTRESULT" , "match_part" , $start , $end , $e , $IDs["$s,$q"]['strand'] , '.' , "ID=$s.$count.$hsp;Parent=$s.$count;Target=$q $qs $qe $q_strand"));
        $last_q = $q;
      }
    }
  }

  // Now can print a parent gff line and all the children.
  // Note: the evalues seem to be sorted properly without actually sorting them.
  // @todo: need to make sure this is always true.
  foreach ($IDs as $sq => $value ) {
    list ($s,$q) = preg_split('/,/' , $sq);
    $evalue =  $IDs["$s,$q"]['E'];
    $parent =  join ("\t", array($s, "BLASTRESULT" , "match" , $IDs["$s,$q"]['SS'] , $IDs["$s,$q"]['SE'] , $IDs["$s,$q"]['E'] , $IDs["$s,$q"]['strand'] , '.' , "ID=$s.$count;Name=$q($evalue)")) . "\n";
    $child = join ("\n",$IDs[$sq]['HSPs']) . "\n";
    fwrite($gff,$parent);
    fwrite($gff,$child);
  }

  // Close the files.
  fclose($tsv);
  fclose($gff);
}


/**ADDED BY SHERI SANDERS October 2016
 * Takes the tsv file and pulls the required parts to build a link to jbrowse
 * with an added track "Blast Results" and each hit as a feature.
 * Link automatically locates to the first hit. 
*/

function convert_tsv2jblink($blast_tsv){

  // Open the TSV file to read from.
  $tsv = fopen($blast_tsv, "r") or die("Unable to open tsv file!");
  $list = array();  
  $first = "TRUE";

  // For each line in the TSV file...
  while(!feof($tsv)) {
    $line = fgets($tsv);
    $line = rtrim($line);

    // Skip the line if it's empty.
    if (preg_match('/^#/',$line) or preg_match('/^\s*$/',$line)){
      continue;
    }

    // Each line has the following parts:
    //  0: query id,
    //  1: subject id,
    //  2: % identity,
    //  3: alignment length,
    //  4: mismatches,
    //  5: gap opens,
    //  6: q. start,
    //  7: q. end,
    //  8: s. start,
    //  9: s. end,
    // 10: evalue,
    // 11: bit score
    $parts = preg_split('/\t/', $line);

    // Assign the important parts of the time to readable variables.
    $start = $parts[8];
    $end = $parts[9];
    $hitname = $parts[1];
    $target = $parts[0];

    //Save first location to allow browser to start there
    if ($first=="TRUE"){
      $loc = join("", array($hitname, "%3A", $start, "..", $end));
    }

    if ($start>$end)
       $features = join("", array("{%22seq_id%22%3A%22", $hitname, "%22%2C%20%22start%22%3A%20", $end, "%2C%20%22end%22%3A%20", $start, "%2C%20%22name%22%3A%20%22", $target, "%22}"));
    if ($start<$end)
       $features = join("", array("{%22seq_id%22%3A%22", $hitname, "%22%2C%20%22start%22%3A%20", $start, "%2C%20%22end%22%3A%20", $end, "%2C%20%22name%22%3A%20%22", $target, "%22}"));
    array_push($list, $features);  

    $track = join("", array("&loc=", $loc, "&addTracks=[{%22label%22%3A%22blast%22%2C%22store%22%3A%22url%22%2C%22type%22%3A%22JBrowse%2FView%2FTrack%2FCanvasFeatures%22%2C%20%22key%22%3A%22Blast%20Results%22}]"));
  
    $feature_list="";
    foreach ($list as $i) {
       $feature_list= join(",", array($feature_list, $i));
    }
    $first="FALSE";
  }  
    $link = join("", array("&addFeatures=[", substr($feature_list,1), "]"));
    $link = join("", array($track,$link));
    return $link;
}