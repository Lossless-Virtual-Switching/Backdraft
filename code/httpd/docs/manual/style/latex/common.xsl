<?xml version="1.0"?>

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
              xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                  xmlns="http://www.w3.org/1999/xhtml">

<!-- Constants used for case translation -->
<xsl:variable name="lowercase" select="'abcdefghijklmnopqrstuvwxyz'" />
<xsl:variable name="uppercase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'" />

<!-- ==================================================================== -->
<!-- Process a documentation section                                      -->
<!-- ==================================================================== -->
<xsl:template match="section">
<xsl:apply-templates/>
</xsl:template>

<xsl:template match="section/title">
<xsl:text>\subsection*{</xsl:text>
<xsl:apply-templates/>
<xsl:text>}</xsl:text><xsl:call-template name="label"/>
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template match="section/section/title">
<xsl:text>\subsubsection*{</xsl:text>
<xsl:apply-templates/>
<xsl:text>}</xsl:text><xsl:call-template name="label"/>
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template match="section/section/section/title">
<xsl:text>\subsubsection*{</xsl:text>
<xsl:apply-templates/>
<xsl:text>}</xsl:text><xsl:call-template name="label"/>
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template match="note/title"></xsl:template>
<xsl:template match="example/title"></xsl:template>

<xsl:template name="label">
<xsl:if test="../@id">
<xsl:variable name="metafile" select="document(/*/@metafile)/metafile" />
<xsl:text>\label{</xsl:text><xsl:value-of 
select="$metafile/path"/><xsl:value-of 
select="$metafile/basename"/>
<xsl:text>:</xsl:text>
<xsl:value-of select="../@id"/>
<xsl:text>}</xsl:text>
</xsl:if>
</xsl:template>


<!-- ==================================================================== -->
<!-- <example>                                                            -->
<!-- ==================================================================== -->
<!-- verbatim inside of fbox doesn't work for some reason -->
<xsl:template match="example">
<xsl:text>\par\smallskip\begin{center}</xsl:text>
<xsl:if test="not(pre) and not(highlight)"><xsl:text>\fbox{</xsl:text></xsl:if>
<xsl:text>\begin{minipage}{.8\textwidth}\begin{flushleft}</xsl:text>
<xsl:apply-templates select="title" mode="print" />
<xsl:text>{\ttfamily\small </xsl:text>
<xsl:text>\noindent </xsl:text><xsl:apply-templates/>
<xsl:text>}</xsl:text>
<xsl:text>\end{flushleft}\end{minipage}</xsl:text>
<xsl:if test="not(pre) and not(highlight)"><xsl:text>}</xsl:text></xsl:if>
<xsl:text>\end{center}\par\smallskip</xsl:text>
</xsl:template>

<xsl:template match="example/title" mode="print">
<xsl:text>\textbf{</xsl:text>
    <xsl:apply-templates/>
<xsl:text>}\\ </xsl:text>
</xsl:template>


<!-- ==================================================================== -->
<!-- indentations                                                         -->
<!-- ==================================================================== -->
<xsl:template match="indent">
<xsl:text>\begin{list}{}{\topsep 0pt\rightmargin 0pt\leftmargin 2em}
\item[] </xsl:text>
<xsl:apply-templates/>
<xsl:text>\end{list}</xsl:text>
</xsl:template>

<!-- ==================================================================== -->
<!-- <note>                                                               -->
<!-- ==================================================================== -->
<xsl:template match="note">
<xsl:text>\par\smallskip
{\Huge </xsl:text>
<xsl:choose>
<xsl:when test="@type='warning'">
  <xsl:text>\fbox{!} </xsl:text>
</xsl:when>
<xsl:otherwise>
  <xsl:text>$\Longrightarrow$</xsl:text>
</xsl:otherwise>
</xsl:choose>
<xsl:text>}\begin{minipage}[t]{.8\textwidth}
\noindent </xsl:text>
<xsl:apply-templates select="title" mode="print" />
<xsl:apply-templates/>
<xsl:text>\end{minipage}\par\smallskip</xsl:text>
</xsl:template>

<xsl:template match="note/title" mode="print">
<xsl:text>\textbf{</xsl:text>
    <xsl:apply-templates/>
<xsl:text>} \\
\noindent </xsl:text>
</xsl:template>


<!-- ==================================================================== -->
<!-- <directive>                                                          -->
<!-- Inserts link to another directive, which might be in another module. -->
<!-- References are converted into lower case.                            -->
<!-- ==================================================================== -->
<xsl:template match="directive" name="directive">
<xsl:text>\textsc{</xsl:text>
<xsl:if test="@type='section'"><xsl:text>\textless{}</xsl:text></xsl:if>
<xsl:choose>
<xsl:when test="@module">
  <xsl:text>\hyperlink{/mod/</xsl:text>
  <xsl:value-of select="normalize-space(@module)"/>
  <xsl:text>:</xsl:text>
  <xsl:choose>
  <xsl:when test="@name">
    <xsl:value-of
        select="normalize-space(translate(@name,$uppercase,$lowercase))"/>
  </xsl:when>
  <xsl:otherwise>
    <xsl:value-of
        select="normalize-space(translate(.,$uppercase,$lowercase))"/>
  </xsl:otherwise>
  </xsl:choose>
  <xsl:text>}{</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>}</xsl:text>
</xsl:when>
<xsl:otherwise>
  <xsl:apply-templates/>
</xsl:otherwise>
</xsl:choose>
<xsl:if test="@type='section'"><xsl:text>\textgreater{}</xsl:text></xsl:if>
<xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="directive" mode="tabular">
<xsl:text>\textsc{</xsl:text>
<xsl:if test="@type='section'"><xsl:text>\textless{}</xsl:text></xsl:if>
<xsl:choose>
<xsl:when test="@module">
  <xsl:text>\hyperlink{/mod/</xsl:text>
  <xsl:value-of select="normalize-space(@module)"/>
  <xsl:text>:</xsl:text>
  <xsl:choose>
  <xsl:when test="@name">
    <xsl:value-of
        select="normalize-space(translate(@name,$uppercase,$lowercase))"/>
  </xsl:when>
  <xsl:otherwise>
    <xsl:value-of
        select="normalize-space(translate(.,$uppercase,$lowercase))"/>
  </xsl:otherwise>
  </xsl:choose>
  <xsl:text>}{</xsl:text>
  <xsl:apply-templates mode="tabular"/>
  <xsl:text>}</xsl:text>
</xsl:when>
<xsl:otherwise>
  <xsl:apply-templates mode="tabular"/>
</xsl:otherwise>
</xsl:choose>
<xsl:if test="@type='section'"><xsl:text>\textgreater{}</xsl:text></xsl:if>
<xsl:text>}</xsl:text>
</xsl:template>

<!-- /directive -->

<!-- ==================================================================== -->
<!-- <module>                                                             -->
<!-- Inserts a link to refereed module                                    -->
<!-- ==================================================================== -->
<xsl:template match="module" name="module">
<xsl:choose>
<xsl:when test="@outdated = 'true'">
  <xsl:text>\textsc{</xsl:text>
    <xsl:apply-templates/>
  <xsl:text>}</xsl:text>
</xsl:when>
<xsl:otherwise>
  <xsl:text>\textsc{\hyperlink{/mod/</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>}{</xsl:text>
      <xsl:apply-templates/>
    <xsl:text>}</xsl:text>
  <xsl:text>}</xsl:text>
</xsl:otherwise>
</xsl:choose>
</xsl:template>

<xsl:template match="module" mode="tabular">
<xsl:choose>
<xsl:when test="@outdated = 'true'">
  <xsl:text>\textsc{</xsl:text>
    <xsl:apply-templates mode="tabular"/>
  <xsl:text>}</xsl:text>
</xsl:when>
<xsl:otherwise>
  <xsl:text>\textsc{\hyperlink{/mod/</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>}{</xsl:text>
      <xsl:apply-templates/>
    <xsl:text>}</xsl:text>
  <xsl:text>}</xsl:text>
</xsl:otherwise>
</xsl:choose>
</xsl:template>
<!-- /module -->



<!-- ==================================================================== -->
<!-- <related>                                                            -->
<!-- ==================================================================== -->
<xsl:template match="related">
<xsl:text>
\fbox{\begin{tabular}{rr}
\begin{minipage}[t]{.49\linewidth}
\textbf{</xsl:text>
<xsl:value-of select="$message[@id='relatedmodules']" />
<xsl:text>} \\
</xsl:text>
<xsl:for-each select="modulelist/module">
<xsl:call-template name="module"/>
<xsl:text> \\
</xsl:text>
</xsl:for-each>
<xsl:text>
\end{minipage} &amp; 
\begin{minipage}[t]{.4\linewidth}
\textbf{</xsl:text>
<xsl:value-of select="$message[@id='relateddirectives']" />
<xsl:text>} \\
</xsl:text>
<xsl:for-each select="directivelist/directive">
<xsl:call-template name="directive"/>
<xsl:text> \\
</xsl:text>
</xsl:for-each>
<xsl:text>
\end{minipage}
\end{tabular}}
\smallskip
</xsl:text>
</xsl:template>


<!-- ==================================================================== -->
<!-- <seealso>                                                            -->
<!-- ==================================================================== -->
<xsl:template name="seealso">
     <xsl:if test="seealso">
	<xsl:text>\textbf{</xsl:text>
        <xsl:value-of select="$message[@id='seealso']" />
        <xsl:text>}\begin{itemize}</xsl:text>
        <xsl:for-each select="seealso">
        <xsl:text>\item </xsl:text>
          <xsl:apply-templates />
        <xsl:text>
</xsl:text>
        </xsl:for-each>
<xsl:text>\end{itemize}</xsl:text>
     </xsl:if>
</xsl:template>


<!-- ==================================================================== -->
<!-- section-title                                                        -->
<!-- ==================================================================== -->
<xsl:template name="section-title">
<xsl:variable name="metafile" select="document(/*/@metafile)/metafile" />
<xsl:text>\section{</xsl:text><xsl:apply-templates select="title"/>
<xsl:text>}</xsl:text>
<xsl:text>\label{</xsl:text><xsl:value-of 
select="$metafile/path"/><xsl:value-of 
select="$metafile/basename"/>
<xsl:text>}
</xsl:text>
</xsl:template>

</xsl:stylesheet>

