#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

/* Shared between parser.c and parser_scan.c. NewConfig is the common
 * ConfigDef constructor: it lives in parser.c (with InitConfigReader)
 * but is also called by InitConfigWriter in parser_scan.c. It is not
 * part of the public parser.h API. */

ConfigDef *NewConfig (char *myname, SyntaxDef * syntax,
											ConfigDataType type, ConfigData source,
											SpecialFunc special, int create);

#endif /* PARSER_INTERNAL_H */
