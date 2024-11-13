<?xml version="1.0" encoding="ISO-8859-1"?>

<xsl:stylesheet
xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
xmlns="http://www.w3.org/TR/REC-html40"
version="2.0">

	<xsl:output method="html"/>

	<!-- Root template -->
	<xsl:template match="/">
		<html>
			<head>
				<title><xsl:value-of select="/root/display_name" /></title>
				<link rel="stylesheet" type="text/css" href="./css/bpdoc.css" />
			</head>
			<body>
				<div id="content_container">
					<xsl:apply-templates />
				</div>
				<script src="./css/collapsible.js" >//</script>
			</body>
		</html>
	</xsl:template>

	<!-- Templates to match specific elements in the input xml -->
	<xsl:template match="/root">
		<h1 class="title_style"><xsl:value-of select="display_name" /> Documentation</h1><br></br>
		<button type="button" onclick="expandAll()">[Expand All]</button>
		<button type="button" onclick="collapseAll()">[Collapse All]</button>
		<xsl:for-each select="plugin">
			<button type="button" class="collapsible">
				<b><xsl:value-of select="display_name" /></b>
				<p><xsl:value-of select="description" /></p>
			</button>
			<div class="collapsible_content">
				<xsl:apply-templates select="modules" />
			</div>
		</xsl:for-each>
	</xsl:template>
	
	<xsl:template match="modules">
		<xsl:apply-templates select="module">
			<xsl:sort select="display_name"/>
		</xsl:apply-templates>
	</xsl:template>
	
	<xsl:template match="module">
		<b><xsl:value-of select="display_name" /></b>
		<xsl:apply-templates select="classes" />
		<br></br>
	</xsl:template>

	<xsl:template match="classes">
		<table>
			<tbody>
				<xsl:apply-templates select="class">
					<xsl:sort select="display_name"/>
				</xsl:apply-templates>
			</tbody>
		</table>
	</xsl:template>

	<xsl:template match="class">
		<tr>
			<td>
				<a>
					<xsl:attribute name="href">./<xsl:value-of select="id" />/<xsl:value-of select="id" />.html</xsl:attribute>
					<xsl:apply-templates select="display_name" />				
				</a>
			</td>
			<td>
				<xsl:apply-templates select="description" />	
			</td>
		</tr>
	</xsl:template>

</xsl:stylesheet>
