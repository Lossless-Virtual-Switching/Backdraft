<?xml version="1.0" encoding="ISO-8859-1"?>
<!DOCTYPE xsl:stylesheet [
    <!ENTITY lf SYSTEM "../xsl/util/lf.xml">
    <!ENTITY % HTTPD-VERSION SYSTEM "../version.ent">
    %HTTPD-VERSION;
]>

<!--
 Licensed to the Apache Software Foundation (ASF) under one or more
 contributor license agreements.  See the NOTICE file distributed with
 this work for additional information regarding copyright ownership.
 The ASF licenses this file to You under the Apache License, Version 2.0
 (the "License"); you may not use this file except in compliance with
 the License.  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-->

<xsl:stylesheet version="1.0"
              xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output 
  method="text"
  encoding="ISO-8859-1"
  indent="no"
/>

<!-- Read the localized messages from the specified language file -->
<xsl:variable name="message" select="document('../lang/en.xml')
                                     /language/messages/message"/>
<xsl:variable name="allmodules" select="document('../xsl/util/allmodules.xml')
                                        /items/item[@lang='en']"/>

<!-- Get the guts of the stylesheets -->
<xsl:include href="manualpage.xsl" />
<xsl:include href="common.xsl" />
<xsl:include href="html.xsl" />
<xsl:include href="synopsis.xsl" />
<xsl:include href="moduleindex.xsl" />
<xsl:include href="directiveindex.xsl" />
<xsl:include href="faq.xsl" />
<xsl:include href="quickreference.xsl" />

<xsl:template match="sitemap">
<xsl:text>
\documentclass[10pt]{book}
\usepackage{times}
\usepackage{longtable}
\usepackage{style/latex/atbeginend}
\usepackage[pdftex]{graphicx}
\usepackage[colorlinks=true,letterpaper=true,linkcolor=blue,urlcolor=blue]{hyperref}

% Let LaTeX be lenient about very-bad line wrapping.
\tolerance=9999 
\emergencystretch=60pt

% Adjust margins to a reasonable level
\topmargin 0pt
\advance \topmargin by -\headheight
\advance \topmargin by -\headsep
\textheight 8.9in
\oddsidemargin 0pt
\evensidemargin \oddsidemargin
\marginparwidth 0.5in
\textwidth 6.5in

% Keep paragraphs flush left (rather than the default of indenting
% the first line) and put a space between paragraphs.
\setlength{\parindent}{0ex}
\addtolength{\parskip}{1.2ex}

% Make space in TOC between section numbers and section title (large numbers!)
\makeatletter
\renewcommand*\l@section{\@dottedtocline{1}{1.5em}{3.6em}}
\makeatother

% Shrink the inter-item spaces
\AfterBegin{itemize}{\setlength{\itemsep}{0em}}

\pagestyle{headings}

\hypersetup{
    pdftitle={</xsl:text>
<xsl:value-of select="$message[@id='apache']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='http-server']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='documentation']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='version']" />
<xsl:text>},
    pdfauthor={Apache Software Foundation}
  }

\title{</xsl:text>
<xsl:value-of select="$message[@id='apache']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='http-server']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='documentation']" />
<xsl:text> </xsl:text>
<xsl:value-of select="$message[@id='version']" />
<xsl:text>\\ \bigskip \bigskip
\includegraphics{images/feather}\\ \bigskip}
\author{Apache Software Foundation}
\date{\today}

\begin{document}
\frontmatter
\maketitle

\section*{About The PDF Documentation}

Licensed to the Apache Software Foundation (ASF) under one or more
contributor license agreements.  See the NOTICE file distributed with
this work for additional information regarding copyright ownership.
The ASF licenses this file to You under the Apache License, Version 2.0
(the "License"); you may not use this file except in compliance with
the License.  You may obtain a copy of the License at \href{http://www.apache.org/licenses/LICENSE-2.0}{http://www.apache.org/licenses/LICENSE-2.0}

This version of the Apache HTTP Server Documentation is converted from
XML source files to \LaTeX\ using XSLT with the help of Apache Ant,
Apache XML Xalan, and Apache XML Xerces.

Since the HTML version of the documentation is more commonly checked
during development, the PDF version may contain some errors and
inconsistencies, especially in formatting.  If you have difficulty
reading a part of this file, please consult the HTML version
of the documentation on the Apache HTTP Server website at
\href{http://httpd.apache.org/docs/&httpd.docs;/}{http://httpd.apache.org/docs/&httpd.docs;/}

The Apache HTTP Server Documentation is maintained by the Apache HTTP
Server Documentation Project.  More information is available at
\href{http://httpd.apache.org/docs-project/}{http://httpd.apache.org/docs-project/}

\tableofcontents
\mainmatter
</xsl:text>

<xsl:for-each select="category">
  <xsl:text>\chapter{</xsl:text>
  <xsl:apply-templates select="title" mode="printcat"/>
  <xsl:text>}
</xsl:text>
    <xsl:apply-templates />
    <xsl:if test="@id = 'modules'">
        <xsl:text>\include{mod/module-dict}</xsl:text>&lf;
        <xsl:text>\include{mod/directive-dict}</xsl:text>&lf;
        <xsl:apply-templates select="document($allmodules)/modulefilelist" />
    </xsl:if>
    <xsl:if test="@id = 'index'">
        <xsl:text>\include{mod/index}</xsl:text>&lf;
        <xsl:text>\include{mod/quickreference}</xsl:text>&lf;
    </xsl:if>
</xsl:for-each>

<xsl:text>\end{document}</xsl:text>
</xsl:template>

<xsl:template match="page">
<xsl:if test="not(starts-with(@href,'http:') or starts-with(@href, 'https:') or starts-with(@href, 'mod/'))">
  <xsl:text>\include{</xsl:text>
  <xsl:choose>
  <xsl:when test="contains(@href,'.')">
    <xsl:value-of select="substring-before(@href,'.')"/>
  </xsl:when>
  <xsl:otherwise>
    <xsl:value-of select="concat(@href,'index')"/>
  </xsl:otherwise>
  </xsl:choose>
  <xsl:text>}</xsl:text>&lf;
</xsl:if>
</xsl:template>

<xsl:template match="category/title" mode="printcat">
<xsl:apply-templates/>
</xsl:template>

<xsl:template match="category/title"></xsl:template>

<xsl:template match="modulefilelist">
<xsl:apply-templates/>
</xsl:template>

<xsl:template match="modulefile">
<xsl:text>\include{mod/</xsl:text>
<xsl:value-of select="substring-before(.,'.')"/>
<xsl:text>}</xsl:text>&lf;
</xsl:template>

<xsl:template match="summary">
<xsl:apply-templates/>
</xsl:template>

<xsl:template name="replace-string">
  <xsl:param name="text"/>
  <xsl:param name="replace"/>
  <xsl:param name="with"/>
    
  <xsl:choose>
    <xsl:when test="not(contains($text,$replace))">
      <xsl:value-of select="$text"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="substring-before($text,$replace)"/>
      <xsl:value-of select="$with"/>
      <xsl:call-template name="replace-string">
        <xsl:with-param name="text" select="substring-after($text,$replace)"/>
        <xsl:with-param name="replace" select="$replace"/>
        <xsl:with-param name="with" select="$with"/>
       </xsl:call-template>
     </xsl:otherwise>
   </xsl:choose>
</xsl:template>

<!-- ==================================================================== -->
<!-- Take care of all the LaTeX special characters.                       -->
<!-- Silly multi-variable technique used to avoid deep recursion.         -->
<!-- ==================================================================== -->
<xsl:template match="text()|@*" mode="tabular">
<xsl:call-template name="ltescape">
  <xsl:with-param name="string" select="."/>
</xsl:call-template>
</xsl:template>

<xsl:template match="text()">
<xsl:call-template name="ltescape">
  <xsl:with-param name="string" select="."/>
</xsl:call-template>
</xsl:template>


<xsl:template name="ltescape">
<xsl:param name="string"/>

<xsl:variable name="result1">
 <xsl:choose>
 <xsl:when test="contains($string, '\')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'\'"/>
    <xsl:with-param name="with" select="'\textbackslash '"/>
    <xsl:with-param name="text" select="normalize-space($string)"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$string"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<xsl:variable name="result2">
 <xsl:choose>
 <xsl:when test="contains($result1, '$')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'$'"/>
    <xsl:with-param name="with" select="'\$'"/>
    <xsl:with-param name="text" select="$result1"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result1"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<xsl:variable name="result3">
 <xsl:choose>
 <xsl:when test="contains($result2, '{')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'{'"/>
    <xsl:with-param name="with" select="'\{'"/>
    <xsl:with-param name="text" select="$result2"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result2"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<xsl:variable name="result4">
 <xsl:choose>
 <xsl:when test="contains($result3, '}')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'}'"/>
    <xsl:with-param name="with" select="'\}'"/>
    <xsl:with-param name="text" select="$result3"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result3"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<!-- The '[' and ']' characters don't, in general, need to be
  escaped.  But there are times when it is ambiguous whether
  [ is the beginning of an optional argument or a literal '['.
  Hence, it is safer to protect the literal ones with {}. -->
<xsl:variable name="result5">
 <xsl:choose>
 <xsl:when test="contains($result4, '[')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'['"/>
    <xsl:with-param name="with" select="'{[}'"/>
    <xsl:with-param name="text" select="$result4"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result4"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<xsl:variable name="result6">
 <xsl:choose>
 <xsl:when test="contains($result5, ']')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="']'"/>
    <xsl:with-param name="with" select="'{]}'"/>
    <xsl:with-param name="text" select="$result5"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result5"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

<xsl:variable name="result7">
 <xsl:choose>
 <xsl:when test="contains($result6, '&quot;')">
   <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'&quot;'"/>
    <xsl:with-param name="with" select="'\texttt{&quot;}'"/>
    <xsl:with-param name="text" select="$result6"/>
   </xsl:call-template>
 </xsl:when>
 <xsl:otherwise>
   <xsl:value-of select="$result6"/>
 </xsl:otherwise>
 </xsl:choose>
</xsl:variable>

  <xsl:call-template name="replace-string">
  <xsl:with-param name="replace" select="'&#8212;'" />
  <xsl:with-param name="with" select="'-'" />
  <xsl:with-param name="text">
    <xsl:call-template name="replace-string">
    <xsl:with-param name="replace" select="'_'"/>
    <xsl:with-param name="with" select="'\_'"/>
    <xsl:with-param name="text">
      <xsl:call-template name="replace-string">
      <xsl:with-param name="replace" select="'#'"/>
      <xsl:with-param name="with" select="'\#'"/>
      <xsl:with-param name="text">
        <xsl:call-template name="replace-string">
        <xsl:with-param name="replace" select="'%'"/>
        <xsl:with-param name="with" select="'\%'"/>
        <xsl:with-param name="text">
          <xsl:call-template name="replace-string">
          <xsl:with-param name="replace" select="'&gt;'"/>
          <xsl:with-param name="with" select="'\textgreater{}'"/>
          <xsl:with-param name="text">
            <xsl:call-template name="replace-string">
            <xsl:with-param name="replace" select="'&lt;'"/>
            <xsl:with-param name="with" select="'\textless{}'"/>
            <xsl:with-param name="text">
              <xsl:call-template name="replace-string">
              <xsl:with-param name="replace" select="'~'"/>
              <xsl:with-param name="with" select="'\textasciitilde{}'"/>
              <xsl:with-param name="text">
                <xsl:call-template name="replace-string">
                <xsl:with-param name="replace" select="'^'"/>
                <xsl:with-param name="with" select="'\^{}'"/>
                <xsl:with-param name="text">
                    <xsl:call-template name="replace-string">
                    <xsl:with-param name="replace" select="'&amp;'"/>
                    <xsl:with-param name="with" select="'\&amp;'"/>
                    <xsl:with-param name="text" select="$result7"/>
                    </xsl:call-template>
                </xsl:with-param>
                </xsl:call-template>
              </xsl:with-param>
              </xsl:call-template>
            </xsl:with-param>
            </xsl:call-template>
          </xsl:with-param>
          </xsl:call-template>
        </xsl:with-param>
        </xsl:call-template>
      </xsl:with-param>
      </xsl:call-template>
    </xsl:with-param>
    </xsl:call-template>
  </xsl:with-param>
  </xsl:call-template>

</xsl:template>

</xsl:stylesheet>
